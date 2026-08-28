/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "asr/model/private_include/audio_decoder.h"
#include "asr/model/private_include/fbank_extractor.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "kaldi-native-fbank/csrc/mel-computations.h"
#include "kaldi-native-fbank/csrc/stft.h"

#include "common/logger.h"

namespace qifeng {
    namespace asr {

        // ── Helper: align and pad PCM data to segment boundaries ──
        void AlignPcmToSegment(std::unique_ptr<std::vector<float>>& pcmData, int64_t maxAudioSeconds,
                               int64_t sampleRate) {
            std::size_t threshold = static_cast<std::size_t>(maxAudioSeconds * sampleRate);
            if (pcmData->size() > threshold) {
                SLOG_WARN << "PCM Data is truncated to " << maxAudioSeconds << " seconds";
                pcmData->resize(threshold);
            } else {
                int64_t audioSeconds = static_cast<int64_t>(pcmData->size() / sampleRate);
                if (pcmData->size() % static_cast<std::size_t>(sampleRate) != 0) {
                    audioSeconds++;
                    SLOG_WARN << "PCM Data is aligned to " << audioSeconds << " seconds";
                    pcmData->resize(static_cast<std::size_t>(audioSeconds * sampleRate));
                }
            }
        }

        static const knf::Stft& GetOrInitStft() {
            static const knf::Stft stft = []() {
                knf::StftConfig cfg;
                cfg.n_fft = 400;
                cfg.hop_length = 160;
                cfg.win_length = 400;
                cfg.window_type = "hann";
                cfg.center = true;
                cfg.pad_mode = "reflect";
                cfg.normalized = false;
                return knf::Stft {cfg};
            }();
            return stft;
        }

        static const std::vector<float>& GetOrInitMelMatrixFlat() {
            static const std::vector<float> melMatrixFlat = []() {
                knf::FrameExtractionOptions frameOpts;
                frameOpts.samp_freq = SAMPLE_RATE;
                frameOpts.frame_length_ms = 1000.0f * 400 / SAMPLE_RATE;
                frameOpts.frame_shift_ms = 1000.0f * 160 / SAMPLE_RATE;
                frameOpts.round_to_power_of_two = false;
                knf::MelBanksOptions melOpts;
                melOpts.num_bins = 128;
                melOpts.low_freq = 0.0f;
                melOpts.high_freq = SAMPLE_RATE / 2.0f;
                melOpts.is_librosa = true;
                melOpts.use_slaney_mel_scale = true;
                melOpts.norm = "slaney";
                knf::MelBanks melBanks(melOpts, frameOpts, 1.0f);
                return melBanks.GetMatrix();
            }();
            return melMatrixFlat;
        }

        static const std::vector<MelBinRange>& GetOrInitMelRanges() {
            constexpr int32_t kNfft = 400;
            constexpr int32_t kNumFreqBins = kNfft / 2 + 1;
            constexpr int32_t kNumMelBins = 128;
            static const std::vector<MelBinRange> melRanges = []() {
                const auto& matrix = GetOrInitMelMatrixFlat();
                std::vector<MelBinRange> ranges(static_cast<std::size_t>(kNumMelBins));
                for (int32_t m = 0; m < kNumMelBins; ++m) {
                    const float* row = matrix.data() + static_cast<std::size_t>(m) * kNumFreqBins;
                    int32_t start = 0, end = 0;
                    for (int32_t f = 0; f < kNumFreqBins; ++f) {
                        if (row[f] != 0.0f) {
                            start = f;
                            break;
                        }
                    }
                    for (int32_t f = kNumFreqBins - 1; f >= 0; --f) {
                        if (row[f] != 0.0f) {
                            end = f + 1;
                            break;
                        }
                    }
                    ranges[static_cast<std::size_t>(m)] = {start, end};
                }
                return ranges;
            }();
            return melRanges;
        }

        // ── Helper: process one segment of mel spectrogram ──
        static void ProcessMelSegment(const knf::StftResult& stftResult, const MelConfig& melCfg, int32_t segIdx,
                                      float* resultPtr) {
            constexpr int32_t kNfft = 400;
            constexpr int32_t kNumFreqBins = kNfft / 2 + 1;
            constexpr int32_t kNumMelBins = 128;

            std::size_t segBase = static_cast<std::size_t>(segIdx) * melCfg.featureSize * melCfg.frameLength;
            int32_t frameStart = segIdx * static_cast<int32_t>(melCfg.frameLength);
            int32_t frameEnd = frameStart + static_cast<int32_t>(melCfg.frameLength);
            std::vector<float> powerFrame(static_cast<std::size_t>(kNumFreqBins));
            for (int32_t t = frameStart; t < frameEnd; ++t) {
                int32_t frameIdx = t - frameStart;
                for (int32_t f = 0; f < kNumFreqBins; ++f) {
                    int32_t idx = t * kNumFreqBins + f;
                    float r = stftResult.real[static_cast<std::size_t>(idx)];
                    float i = stftResult.imag[static_cast<std::size_t>(idx)];
                    powerFrame[static_cast<std::size_t>(f)] = r * r + i * i;
                }
                for (int32_t m = 0; m < kNumMelBins; ++m) {
                    const float* melRow =
                        melCfg.matrix->data() + static_cast<std::size_t>(m) * static_cast<std::size_t>(kNumFreqBins);
                    const MelBinRange& range = (*melCfg.ranges)[static_cast<std::size_t>(m)];
                    float sum = 0.0f;
                    for (int32_t f = range.start; f < range.end; ++f) {
                        sum += melRow[static_cast<std::size_t>(f)] * powerFrame[static_cast<std::size_t>(f)];
                    }
                    resultPtr[segBase + static_cast<std::size_t>(m) * melCfg.frameLength +
                              static_cast<std::size_t>(frameIdx)] = sum;
                }
            }
        }

        // ── Helper: compute mel spectrogram via STFT + mel filtering ──
        std::vector<float> ComputeMelSpectrogram(const float* pcmData, int32_t numSamples, uint32_t featureSize,
                                                 uint32_t frameLength, int64_t sampleRate) {
            (void)sampleRate;
            const knf::Stft& stft = GetOrInitStft();
            knf::StftResult stftResult = stft.Compute(pcmData, numSamples);
            const int32_t kNumFrames = stftResult.num_frames;
            if (kNumFrames <= 1) {
                throw std::runtime_error {"Fbank feature extraction failed: no available frames."};
            }
            int32_t actualNumFrames = kNumFrames - 1;

            const std::vector<float>& melMatrixFlat = GetOrInitMelMatrixFlat();
            const std::vector<MelBinRange>& melRanges = GetOrInitMelRanges();
            MelConfig melCfg {&melMatrixFlat, &melRanges, featureSize, frameLength};

            int32_t numSegments = actualNumFrames / static_cast<int32_t>(frameLength);
            if (numSegments == 0) {
                throw std::runtime_error {"Fbank feature extraction failed: audio too short for one segment. "
                                          "actualNumFrames=" +
                                          std::to_string(actualNumFrames) +
                                          " < mAudioSegmentFrameLength=" + std::to_string(frameLength)};
            }

            std::size_t resultSize = static_cast<std::size_t>(numSegments) * featureSize * frameLength;
            std::vector<float> result(resultSize);
            float* resultPtr = result.data();

#if defined(_OPENMP)
    #pragma omp parallel for schedule(static) if (numSegments > 1)
#endif
            for (int32_t segIdx = 0; segIdx < numSegments; ++segIdx) {
                ProcessMelSegment(stftResult, melCfg, segIdx, resultPtr);
            }
            return result;
        }

        // ── Helper: post-process fbank features (clamp → log10 → dynamic range → normalize) ──
        void PostProcessFbankFeatures(std::vector<float>& features) {
            float maxVal = -std::numeric_limits<float>::max();
            for (float& v : features) {
                if (v < 1e-10f) {
                    v = 1e-10f;
                }
                v = std::log10(v);
                if (v > maxVal) {
                    maxVal = v;
                }
            }
            float thresholdVal = maxVal - 8.0f;
            for (float& v : features) {
                if (v < thresholdVal) {
                    v = thresholdVal;
                }
                v = (v + 4.0f) / 4.0f;
            }
        }

    }  // namespace asr
}  // namespace qifeng
