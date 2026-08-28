/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "models/worker/audio_feature.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "common/config_manager.h"
#include "common/logger.h"

#include <NumCpp.hpp>
#include <kaldi-native-fbank/csrc/feature-fbank.h>
#include <kaldi-native-fbank/csrc/feature-window.h>

namespace qifeng {

    nc::NdArray<float> AudioFeature::mAsrCmvnData;
    nc::NdArray<float> AudioFeature::mVadCmvnData;
    std::mutex AudioFeature::mCmvnMutex;
    std::atomic<bool> AudioFeature::mInitialized(false);

    constexpr float AudioFeature::kEpsilon;
    constexpr float AudioFeature::kMillisecondsToSeconds;
    constexpr float AudioFeature::kMelScaleFactor;
    constexpr float AudioFeature::kMelScaleOffset;

    bool AudioFeature::InitializeCmvn() {
        if (mInitialized.load(std::memory_order_acquire)) {
            FLOG_DEBUG("CMVN data already initialized");
            return true;
        }

        std::lock_guard<std::mutex> lock(mCmvnMutex);
        if (mInitialized.load(std::memory_order_relaxed)) {
            FLOG_DEBUG("CMVN data already initialized");
            return true;
        }

        SLOG_INFO << "AudioFeature: Start initializing CMVN data ";

        mAsrCmvnData = nc::NdArray<float>();
        mVadCmvnData = nc::NdArray<float>();

        try {
            std::string asrCmvPath = ConfigManager::GetInstance().GetString(
                "models", "asr_cmvn",
                "/data/aas/model/speech_seaco_paraformer_large_asr_nat-zh-cn-16k-common-vocab8404-pytorch/am.mvn");
            std::string vadCmvPath = ConfigManager::GetInstance().GetString(
                "models", "vad_cmvn", "/data/aas/model/speech_fsmn_vad_zh-cn-16k-common/am.mvn");

            if (!asrCmvPath.empty()) {
                SLOG_INFO << "AudioFeature: Loading ASR CMVN data: " << asrCmvPath;
                nc::NdArray<float> asrData = LoadCmvn(asrCmvPath);
                if (asrData.isempty()) {
                    SLOG_ERROR << "AudioFeature: Failed to load ASR CMVN data: " << asrCmvPath;
                } else {
                    mAsrCmvnData = std::move(asrData);
                    SLOG_INFO << "AudioFeature: ASR CMVN data loaded successfully";
                }
            } else {
                SLOG_WARN << "AudioFeature: ASR CMVN config path not found";
            }

            if (!vadCmvPath.empty()) {
                SLOG_INFO << "AudioFeature: Loading VAD CMVN data: " << vadCmvPath;
                nc::NdArray<float> vadData = LoadCmvn(vadCmvPath);
                if (vadData.isempty()) {
                    SLOG_ERROR << "AudioFeature: Failed to load VAD CMVN data: " << vadCmvPath;
                } else {
                    mVadCmvnData = std::move(vadData);
                    SLOG_INFO << "AudioFeature: VAD CMVN data loaded successfully";
                }
            } else {
                SLOG_WARN << "AudioFeature: VAD CMVN config path not found";
            }

            mInitialized.store(true, std::memory_order_release);
            return true;
        } catch (const std::exception& e) {
            SLOG_ERROR << "AudioFeature: Exception while initializing CMVN data: " << e.what();
            mAsrCmvnData = nc::NdArray<float>();
            mVadCmvnData = nc::NdArray<float>();
            return false;
        } catch (...) {
            SLOG_ERROR << "AudioFeature: Unknown exception while initializing CMVN data";
            return false;
        }
    }

    void AudioFeature::ReleaseCmvn() {
        std::lock_guard<std::mutex> lock(mCmvnMutex);

        mAsrCmvnData = nc::NdArray<float>();
        mVadCmvnData = nc::NdArray<float>();

        mInitialized.store(false, std::memory_order_release);
        SLOG_INFO << "AudioFeature: CMVN data released";
    }

    nc::NdArray<float> AudioFeature::LoadCmvn(const std::string& cmvnFile) {
        SLOG_INFO << "AudioFeature: Start loading CMVN file: " << cmvnFile;

        std::ifstream file(cmvnFile);
        if (!file.is_open()) {
            SLOG_ERROR << "AudioFeature: Cannot open CMVN file: " << cmvnFile << ", error: " << strerror(errno);
            return nc::NdArray<float>();
        }

        std::vector<std::string> lines;
        std::string line;
        size_t lineNumber = 0;

        try {
            while (std::getline(file, line)) {
                lineNumber++;
                lines.push_back(line);
            }
            file.close();

            SLOG_DEBUG << "AudioFeature: Read CMVN file, total lines: " << lines.size();

            std::vector<float> meansList;
            std::vector<float> varsList;
            bool foundMeans = false;
            bool foundVars = false;

            for (size_t i = 0; i < lines.size(); ++i) {
                std::istringstream iss(lines[i]);
                std::vector<std::string> lineItem;
                std::string token;
                while (iss >> token) {
                    lineItem.push_back(token);
                }

                if (lineItem.empty())
                    continue;

                if (lineItem[0] == "<AddShift>") {
                    if (i + 1 < lines.size()) {
                        std::istringstream issNext(lines[i + 1]);
                        std::vector<std::string> nextLineItem;
                        while (issNext >> token) {
                            nextLineItem.push_back(token);
                        }

                        if (!nextLineItem.empty() && nextLineItem[0] == "<LearnRateCoef>") {
                            if (nextLineItem.size() < 5) {
                                SLOG_ERROR << "AudioFeature: CMVN format error at line " << (i + 2)
                                           << ", insufficient data after <LearnRateCoef>";
                                return nc::NdArray<float>();
                            }

                            meansList.clear();
                            for (size_t j = 3; j < nextLineItem.size() - 1; ++j) {
                                try {
                                    meansList.push_back(std::stof(nextLineItem[j]));
                                } catch (const std::invalid_argument& e) {
                                    SLOG_ERROR << "AudioFeature: CMVN parse error at line " << (i + 2) << ", col "
                                               << (j + 1) << ": " << nextLineItem[j];
                                    return nc::NdArray<float>();
                                } catch (const std::out_of_range& e) {
                                    SLOG_ERROR << "AudioFeature: CMVN range error at line " << (i + 2) << ", col "
                                               << (j + 1) << ": " << nextLineItem[j];
                                    return nc::NdArray<float>();
                                }
                            }
                            foundMeans = true;
                            SLOG_DEBUG << "AudioFeature: Extracted means, dim: " << meansList.size();
                        }
                    } else {
                        SLOG_ERROR << "AudioFeature: CMVN format error at line " << (i + 1)
                                   << ", missing data after <AddShift>";
                        return nc::NdArray<float>();
                    }
                } else if (lineItem[0] == "<Rescale>") {
                    if (i + 1 < lines.size()) {
                        std::istringstream issNext(lines[i + 1]);
                        std::vector<std::string> nextLineItem;
                        while (issNext >> token) {
                            nextLineItem.push_back(token);
                        }

                        if (!nextLineItem.empty() && nextLineItem[0] == "<LearnRateCoef>") {
                            if (nextLineItem.size() < 5) {
                                SLOG_ERROR << "AudioFeature: CMVN format error at line " << (i + 2)
                                           << ", insufficient data after <LearnRateCoef>";
                                return nc::NdArray<float>();
                            }

                            varsList.clear();
                            for (size_t j = 3; j < nextLineItem.size() - 1; ++j) {
                                try {
                                    varsList.push_back(std::stof(nextLineItem[j]));
                                } catch (const std::invalid_argument& e) {
                                    SLOG_ERROR << "AudioFeature: CMVN parse error at line " << (i + 2) << ", col "
                                               << (j + 1) << ": " << nextLineItem[j];
                                    return nc::NdArray<float>();
                                } catch (const std::out_of_range& e) {
                                    SLOG_ERROR << "AudioFeature: CMVN range error at line " << (i + 2) << ", col "
                                               << (j + 1) << ": " << nextLineItem[j];
                                    return nc::NdArray<float>();
                                }
                            }
                            foundVars = true;
                            SLOG_DEBUG << "AudioFeature: Extracted vars, dim: " << varsList.size();
                        }
                    } else {
                        SLOG_ERROR << "AudioFeature: CMVN format error at line " << (i + 1)
                                   << ", missing data after <Rescale>";
                        return nc::NdArray<float>();
                    }
                }
            }

            if (!foundMeans) {
                SLOG_ERROR << "AudioFeature: CMVN format error, <AddShift> section not found";
                return nc::NdArray<float>();
            }

            if (!foundVars) {
                SLOG_ERROR << "AudioFeature: CMVN format error, <Rescale> section not found";
                return nc::NdArray<float>();
            }

            if (meansList.empty() || varsList.empty()) {
                SLOG_ERROR << "AudioFeature: CMVN format error, means or vars data is empty";
                return nc::NdArray<float>();
            }

            if (meansList.size() != varsList.size()) {
                SLOG_ERROR << "AudioFeature: CMVN format error, means and vars dimension mismatch"
                           << " (means: " << meansList.size() << ", vars: " << varsList.size() << ")";
                return nc::NdArray<float>();
            }

            size_t dim = meansList.size();
            SLOG_INFO << "AudioFeature: CMVN file loaded, dim: " << dim;

            nc::NdArray<float> cmvn(2, dim);

            for (size_t j = 0; j < dim; ++j) {
                cmvn(0, j) = meansList[j];
            }

            for (size_t j = 0; j < dim; ++j) {
                cmvn(1, j) = varsList[j];
            }

            return cmvn;
        } catch (const std::exception& e) {
            SLOG_ERROR << "AudioFeature: Exception while loading CMVN file (line " << lineNumber << "): " << e.what();
            file.close();
            return nc::NdArray<float>();
        } catch (...) {
            SLOG_ERROR << "AudioFeature: Unknown exception while loading CMVN file";
            file.close();
            return nc::NdArray<float>();
        }
    }

    nc::NdArray<float> AudioFeature::GetCmvnData(const std::string& cmvnType) {
        if (!mInitialized.load(std::memory_order_acquire)) {
            SLOG_ERROR << "AudioFeature: CMVN data not initialized, call InitializeCmvn first";
            return {};
        }

        nc::NdArray<float> cmvnData = (cmvnType == "vad") ? mVadCmvnData : mAsrCmvnData;

        if (cmvnData.isempty()) {
            SLOG_ERROR << "AudioFeature: CMVN data not loaded for type: " << cmvnType;
        }

        return cmvnData;
    }

    nc::NdArray<float> AudioFeature::ApplyCmvn(const nc::NdArray<float>& inputs, const std::string& cmvnType) {
        if (inputs.isempty()) {
            SLOG_ERROR << "AudioFeature: Empty inputs for CMVN application";
            return inputs;
        }

        nc::NdArray<float> cmvnData = GetCmvnData(cmvnType);
        if (cmvnData.isempty()) {
            return inputs;
        }

        if (cmvnData.shape().rows != 2) {
            SLOG_ERROR << "AudioFeature: CMVN should have 2 rows";
            return inputs;
        }

        if (inputs.shape().cols > cmvnData.shape().cols) {
            SLOG_ERROR << "AudioFeature: Input dimension exceeds CMVN dimension";
            return inputs;
        }

        size_t n = inputs.shape().rows;
        size_t dim = inputs.shape().cols;
        nc::NdArray<float> output(n, dim);

        nc::NdArray<float> means = cmvnData(0, nc::Slice(0, dim));
        nc::NdArray<float> vars = cmvnData(1, nc::Slice(0, dim));

#pragma omp parallel for
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < dim; ++j) {
                output(i, j) = (inputs(i, j) + means[j]) * vars[j];
            }
        }

        return output;
    }

    nc::NdArray<float> AudioFeature::ApplyLfr(const nc::NdArray<float>& inputs, int lfrM, int lfrN) {
        if (inputs.isempty()) {
            SLOG_ERROR << "AudioFeature: Empty input features for LFR";
            return inputs;
        }

        size_t t = inputs.shape().rows;
        size_t featDim = inputs.shape().cols;

        size_t tLfr = static_cast<size_t>(std::ceil(static_cast<float>(t) / lfrN));

        nc::NdArray<float> leftPadding(3, featDim);
        for (size_t i = 0; i < 3; ++i) {
            for (size_t j = 0; j < featDim; ++j) {
                leftPadding(i, j) = inputs(0, j);
            }
        }

        nc::NdArray<float> paddedInputs = nc::vstack({leftPadding, inputs});
        t += (lfrM - 1) / 2;

        nc::NdArray<float> lfrOutputs(tLfr, lfrM * featDim);

        int lastIdx = (static_cast<int>(t) - lfrM) / lfrN + 1;
        int numPadding = lfrM - (static_cast<int>(t) - lastIdx * lfrN);

        if (numPadding > 0) {
            numPadding = static_cast<int>((2 * lfrM - 2 * static_cast<int>(t) + (tLfr - 1 + lastIdx) * lfrN) / 2.0 *
                                          (tLfr - lastIdx));

            nc::NdArray<float> rightPadding(numPadding, featDim);
            for (int i = 0; i < numPadding; ++i) {
                for (size_t j = 0; j < featDim; ++j) {
                    rightPadding(i, j) = inputs(static_cast<int>(t) - 1 - 3, j);
                }
            }

            paddedInputs = nc::vstack({paddedInputs, rightPadding});
        }

        for (size_t tIdx = 0; tIdx < tLfr; ++tIdx) {
            for (int m = 0; m < lfrM; ++m) {
                int inputIdx = tIdx * lfrN + m;
                if (inputIdx >= static_cast<int>(paddedInputs.shape().rows)) {
                    inputIdx = static_cast<int>(paddedInputs.shape().rows) - 1;
                }

                for (size_t d = 0; d < featDim; ++d) {
                    lfrOutputs(tIdx, m * featDim + d) = paddedInputs(inputIdx, d);
                }
            }
        }

        return lfrOutputs;
    }

    int AudioFeature::NextPowerOf2(int x) {
        if (x <= 0)
            return 1;
        uint32_t ux = static_cast<uint32_t>(x - 1);
#if defined(__GNUC__) || defined(__clang__)
        return 1U << (32 - __builtin_clz(ux));
#elif defined(_MSC_VER)
        unsigned long index;
        _BitScanReverse(&index, ux);
        return 1U << (index + 1);
#else
        ux |= ux >> 1;
        ux |= ux >> 2;
        ux |= ux >> 4;
        ux |= ux >> 8;
        ux |= ux >> 16;
        return static_cast<int>(ux + 1);
#endif
    }

    nc::NdArray<float> AudioFeature::GetStrided(const nc::NdArray<float>& waveform, int windowSize, int windowShift,
                                                bool snipEdges) {
        int numSamples = waveform.size();

        int numFrames = 0;
        int pad = 0;

        if (snipEdges) {
            if (numSamples < windowSize) {
                return nc::NdArray<float>();
            }
            numFrames = 1 + (numSamples - windowSize) / windowShift;
        } else {
            numFrames = (numSamples + windowShift / 2) / windowShift;
            pad = windowSize / 2 - windowShift / 2;
        }

        if (numFrames <= 0) {
            return nc::NdArray<float>();
        }

        nc::NdArray<float> frames(numFrames, windowSize);

        for (int i = 0; i < numFrames; ++i) {
            int startMod = i * windowShift;

            for (int j = 0; j < windowSize; ++j) {
                int idxMod = startMod + j;
                float value = 0.0f;

                if (snipEdges) {
                    value = waveform[idxMod];
                } else {
                    if (pad > 0) {
                        if (idxMod < pad) {
                            int origIdx = pad - idxMod - 1;
                            value = waveform[origIdx];
                        } else if (idxMod >= pad + numSamples) {
                            int origIdx = 2 * numSamples + pad - idxMod - 1;
                            value = waveform[origIdx];
                        } else {
                            int origIdx = idxMod - pad;
                            value = waveform[origIdx];
                        }
                    } else {
                        int offset = -pad;
                        int origIdx = idxMod + offset;
                        value = waveform[origIdx];
                    }
                }

                frames(i, j) = value;
            }
        }

        return frames;
    }

    nc::NdArray<float> AudioFeature::FeatureWindowFunction(WindowType windowType, int windowSize, float blackmanCoeff) {
        if (windowSize <= 0) {
            return nc::NdArray<float>();
        }
        if (windowSize == 1) {
            return nc::ones<float>(1);
        }

        switch (windowType) {
            case WindowType::HANNING:
                return nc::hanning(windowSize).astype<float>();

            case WindowType::HAMMING:
                return nc::hamming(windowSize).astype<float>();

            case WindowType::POVEY:
                return nc::power(nc::hanning(windowSize), 0).astype<float>();

            case WindowType::RECTANGULAR:
                return nc::ones<float>(windowSize);

            case WindowType::BLACKMAN: {
                nc::NdArray<float> a = 2.0f * static_cast<float>(nc::constants::pi) * nc::arange<float>(windowSize) /
                                       static_cast<float>(windowSize - 1);
                nc::NdArray<float> window =
                    blackmanCoeff - 0.5f * nc::cos(a) + (0.5f - blackmanCoeff) * nc::cos(2.0f * a);
                return window;
            }

            default:
                return nc::ones<float>(windowSize);
        }
    }

    std::tuple<int, int, int> AudioFeature::GetWindowProperties(float sampleFrequency, float frameShift,
                                                                float frameLength, bool roundToPowerOfTwo) {
        int windowShift = static_cast<int>(sampleFrequency * frameShift * kMillisecondsToSeconds);
        int windowSize = static_cast<int>(sampleFrequency * frameLength * kMillisecondsToSeconds);
        int paddedWindowSize = roundToPowerOfTwo ? NextPowerOf2(windowSize) : windowSize;

        return std::make_tuple(windowShift, windowSize, paddedWindowSize);
    }

    nc::NdArray<float> AudioFeature::GetWindow(const nc::NdArray<float>& waveform, int paddedWindowSize, int windowSize,
                                               int windowShift, WindowType windowType, float blackmanCoeff,
                                               bool snipEdges, float dither, bool removeDcOffset,
                                               float preemphasisCoeff) {
        nc::NdArray<float> stridedInput = GetStrided(waveform, windowSize, windowShift, snipEdges);

        if (dither != 0.0f) {
            nc::NdArray<float> noise = nc::random::randN<float>(stridedInput.shape());
            stridedInput += noise * dither;
        }

        if (removeDcOffset) {
            nc::NdArray<float> meanPerFrame = nc::mean(stridedInput, nc::Axis::COL).astype<float>();
            meanPerFrame.reshape({stridedInput.shape().rows, 1u});
            stridedInput -= meanPerFrame;
        }

        if (preemphasisCoeff != 0.0f) {
            nc::NdArray<float> padded(stridedInput.shape().rows, windowSize + 1);
            padded.put(nc::Slice(), nc::Slice(1, windowSize + 1), stridedInput);
            nc::NdArray<float> firstCol = stridedInput(nc::Slice(), 0);
            padded.put(nc::Slice(), 0, firstCol);

            nc::NdArray<float> paddedShifted = padded(nc::Slice(), nc::Slice(0, windowSize));
            stridedInput = stridedInput - preemphasisCoeff * paddedShifted;
        }

        nc::NdArray<float> windowFunction = FeatureWindowFunction(windowType, windowSize, blackmanCoeff);
        stridedInput = stridedInput * windowFunction;

        if (paddedWindowSize > windowSize) {
            nc::NdArray<float> result(stridedInput.shape().rows, paddedWindowSize);
            result.zeros();
            result.put(nc::Slice(), nc::Slice(0, windowSize), stridedInput);
            return result;
        } else {
            return stridedInput;
        }
    }

    float AudioFeature::MelScaleScalar(float freq) {
        return kMelScaleFactor * std::log(1.0f + freq / kMelScaleOffset);
    }

    nc::NdArray<float> AudioFeature::MelScale(const nc::NdArray<float>& freq) {
        nc::NdArray<float> mel(freq.shape());

#pragma omp parallel for
        for (size_t i = 0; i < freq.size(); ++i) {
            mel[i] = MelScaleScalar(freq[i]);
        }

        return mel;
    }

    float AudioFeature::InverseMelScaleScalar(float melFreq) {
        return kMelScaleOffset * (std::exp(melFreq / kMelScaleFactor) - 1.0f);
    }

    nc::NdArray<float> AudioFeature::InverseMelScale(const nc::NdArray<float>& melFreq) {
        nc::NdArray<float> freq(melFreq.shape());

#pragma omp parallel for
        for (size_t i = 0; i < melFreq.size(); ++i) {
            freq[i] = InverseMelScaleScalar(melFreq[i]);
        }

        return freq;
    }

    nc::NdArray<float> AudioFeature::VtlnWarpFreq(float vtlnLowCutoff, float vtlnHighCutoff, float lowFreq,
                                                  float highFreq, float vtlnWarpFactor,
                                                  const nc::NdArray<float>& freq) {
        float l = vtlnLowCutoff * std::max(1.0f, vtlnWarpFactor);
        float h = vtlnHighCutoff * std::min(1.0f, vtlnWarpFactor);
        float scale = 1.0f / vtlnWarpFactor;
        float fl = scale * l;
        float fh = scale * h;

        float scaleLeft = (fl - lowFreq) / (l - lowFreq);
        float scaleRight = (highFreq - fh) / (highFreq - h);

        nc::NdArray<float> res(freq.shape());

#pragma omp parallel for
        for (size_t i = 0; i < freq.size(); ++i) {
            float f = freq[i];
            bool outsideLowHigh = (f < lowFreq) || (f > highFreq);
            bool beforeL = f < l;
            bool beforeH = f < h;
            bool afterH = f >= h;

            if (afterH && !outsideLowHigh) {
                res[i] = highFreq + scaleRight * (f - highFreq);
            } else if (beforeH && !beforeL && !outsideLowHigh) {
                res[i] = scale * f;
            } else if (beforeL && !outsideLowHigh) {
                res[i] = lowFreq + scaleLeft * (f - lowFreq);
            } else {
                res[i] = f;
            }
        }

        return res;
    }

    nc::NdArray<float> AudioFeature::VtlnWarpMelFreq(float vtlnLowCutoff, float vtlnHighCutoff, float lowFreq,
                                                     float highFreq, float vtlnWarpFactor,
                                                     const nc::NdArray<float>& melFreq) {
        nc::NdArray<float> freq = InverseMelScale(melFreq);
        nc::NdArray<float> warpedFreq =
            VtlnWarpFreq(vtlnLowCutoff, vtlnHighCutoff, lowFreq, highFreq, vtlnWarpFactor, freq);
        return MelScale(warpedFreq);
    }

    nc::NdArray<float> AudioFeature::GetMelBanks(int numBins, int windowLengthPadded, float sampleFreq, float lowFreq,
                                                 float highFreq, float vtlnLow, float vtlnHigh, float vtlnWarpFactor) {
        if (numBins <= 3) {
            SLOG_ERROR << "AudioFeature: Must have at least 3 mel bins";
            return nc::NdArray<float>();
        }

        int numFftBins = windowLengthPadded / 2;
        float nyquist = 0.5f * sampleFreq;

        if (highFreq <= 0.0f) {
            highFreq = nyquist + highFreq;
        }

        float fftBinWidth = sampleFreq / windowLengthPadded;
        float melLowFreq = MelScaleScalar(lowFreq);
        float melHighFreq = MelScaleScalar(highFreq);
        float melFreqDelta = (melHighFreq - melLowFreq) / (numBins + 1);

        if (vtlnHigh < 0.0f) {
            vtlnHigh = nyquist + vtlnHigh;
        }

        nc::NdArray<float> leftMel(numBins, 1);
        nc::NdArray<float> centerMel(numBins, 1);
        nc::NdArray<float> rightMel(numBins, 1);

#pragma omp parallel for
        for (int i = 0; i < numBins; ++i) {
            leftMel[i] = melLowFreq + i * melFreqDelta;
            centerMel[i] = melLowFreq + (i + 1.0f) * melFreqDelta;
            rightMel[i] = melLowFreq + (i + 2.0f) * melFreqDelta;
        }

        if (vtlnWarpFactor != 1.0f) {
            leftMel = VtlnWarpMelFreq(vtlnLow, vtlnHigh, lowFreq, highFreq, vtlnWarpFactor, leftMel);
            centerMel = VtlnWarpMelFreq(vtlnLow, vtlnHigh, lowFreq, highFreq, vtlnWarpFactor, centerMel);
            rightMel = VtlnWarpMelFreq(vtlnLow, vtlnHigh, lowFreq, highFreq, vtlnWarpFactor, rightMel);
        }

        nc::NdArray<float> freqs(numFftBins, 1);
#pragma omp parallel for
        for (int i = 0; i < numFftBins; ++i) {
            freqs[i] = fftBinWidth * i;
        }

        nc::NdArray<float> mel = MelScale(freqs);

        nc::NdArray<float> melBanks(numBins, numFftBins);
        melBanks.zeros();

#pragma omp parallel for collapse(2)
        for (int i = 0; i < numBins; ++i) {
            for (int j = 0; j < numFftBins; ++j) {
                float upSlope = (mel[j] - leftMel[i]) / (centerMel[i] - leftMel[i]);
                float downSlope = (rightMel[i] - mel[j]) / (rightMel[i] - centerMel[i]);
                melBanks(i, j) = std::max(0.0f, std::min(upSlope, downSlope));
            }
        }

        return melBanks;
    }

    nc::NdArray<float> AudioFeature::Fbank(const nc::NdArray<float>& waveform, float blackmanCoeff, float dither,
                                           float frameLength, float frameShift, float highFreq, float lowFreq,
                                           int numMelBins, float preemphasisCoeff, bool removeDcOffset,
                                           bool roundToPowerOfTwo, float sampleFrequency, bool snipEdges,
                                           bool useLogFbank, bool usePower, float vtlnHigh, float vtlnLow,
                                           float vtlnWarp, WindowType windowType) {
        if (waveform.isempty()) {
            SLOG_ERROR << "AudioFeature: Empty input waveform";
            return nc::NdArray<float>();
        }

        try {
            std::vector<float> audioData;
            audioData.reserve(waveform.size());
            for (size_t i = 0; i < waveform.size(); ++i) {
                audioData.push_back(waveform(i, 0));
            }

            knf::FbankOptions opts;

            opts.frame_opts.samp_freq = sampleFrequency;
            opts.frame_opts.frame_shift_ms = frameShift;
            opts.frame_opts.frame_length_ms = frameLength;
            opts.frame_opts.dither = dither;
            opts.frame_opts.preemph_coeff = preemphasisCoeff;
            opts.frame_opts.remove_dc_offset = removeDcOffset;
            opts.frame_opts.round_to_power_of_two = roundToPowerOfTwo;
            opts.frame_opts.blackman_coeff = blackmanCoeff;
            opts.frame_opts.snip_edges = snipEdges;

            switch (windowType) {
                case WindowType::HAMMING:
                    opts.frame_opts.window_type = "hamming";
                    break;
                case WindowType::HANNING:
                    opts.frame_opts.window_type = "hanning";
                    break;
                case WindowType::POVEY:
                    opts.frame_opts.window_type = "povey";
                    break;
                case WindowType::RECTANGULAR:
                    opts.frame_opts.window_type = "rectangular";
                    break;
                case WindowType::BLACKMAN:
                    opts.frame_opts.window_type = "blackman";
                    break;
                default:
                    opts.frame_opts.window_type = "povey";
            }

            opts.mel_opts.num_bins = numMelBins;
            opts.mel_opts.low_freq = lowFreq;
            float nyquist = sampleFrequency / 2.0f;
            opts.mel_opts.high_freq = (highFreq <= 0.0f) ? (nyquist + highFreq) : highFreq;
            opts.mel_opts.vtln_low = vtlnLow;
            opts.mel_opts.vtln_high = (vtlnHigh < 0.0f) ? (nyquist + vtlnHigh) : vtlnHigh;

            opts.use_log_fbank = useLogFbank;
            opts.use_power = usePower;

            knf::FbankComputer fbankComputer(opts);

            int32_t numFrames = knf::NumFrames(audioData.size(), opts.frame_opts, snipEdges);
            if (numFrames <= 0) {
                return nc::NdArray<float>();
            }

            int32_t featureDim = fbankComputer.Dim();
            nc::NdArray<float> melFeatures(numFrames, featureDim);

            std::vector<float> window;
            std::vector<float> feature(featureDim);

            knf::FeatureWindowFunction windowFunction(opts.frame_opts);

            for (int32_t f = 0; f < numFrames; ++f) {
                float logEnergyPreWindow = 0.0f;
                window.clear();

                knf::ExtractWindow(0, audioData, f, opts.frame_opts, windowFunction, &window,
                                   fbankComputer.NeedRawLogEnergy() ? &logEnergyPreWindow : nullptr);

                fbankComputer.Compute(logEnergyPreWindow, vtlnWarp, &window, feature.data());

                for (int32_t d = 0; d < featureDim; ++d) {
                    melFeatures(f, d) = feature[d];
                }
            }

            return melFeatures;
        } catch (const std::exception& e) {
            SLOG_ERROR << "AudioFeature: Exception in kaldi-native-fbank Fbank: " << e.what();
            return nc::NdArray<float>();
        } catch (...) {
            SLOG_ERROR << "AudioFeature: Unknown exception in kaldi-native-fbank Fbank";
            return nc::NdArray<float>();
        }
    }

    std::vector<std::vector<float>> AudioFeature::VadFeature(const std::vector<float>& audioSample) {
        if (audioSample.empty()) {
            SLOG_ERROR << "AudioFeature: Empty input audio sample for VAD feature extraction";
            return {};
        }

        if (!mInitialized.load(std::memory_order_acquire)) {
            SLOG_ERROR << "AudioFeature: CMVN data not initialized, call InitializeCmvn first";
            return {};
        }

        nc::NdArray<float> audioNdArray = FloatVectorToNdArray(audioSample);

        nc::NdArray<float> speechFeat =
            Fbank(audioNdArray, 0.42f, 0.0f, 25.0f, 10.0f, 0.0f, 20.0f, 80, 0.97f, true, true, 16000.0f, true, true,
                  true, -500.0f, 100.0f, 1.0f, WindowType::HAMMING);

        if (speechFeat.isempty()) {
            SLOG_ERROR << "AudioFeature: Failed to compute FBank feature for VAD";
            return {};
        }

        nc::NdArray<float> lfrFeat = ApplyLfr(speechFeat, 5, 1);
        nc::NdArray<float> result = ApplyCmvn(lfrFeat, "vad");

        return NdArrayToFloatMatrix(result);
    }

    std::vector<std::vector<float>> AudioFeature::AsrFeature(const std::vector<float>& audioSample) {
        if (audioSample.empty()) {
            SLOG_ERROR << "AudioFeature: Empty input audio sample for ASR feature extraction";
            return {};
        }

        if (!mInitialized.load(std::memory_order_acquire)) {
            SLOG_ERROR << "AudioFeature: CMVN data not initialized, call InitializeCmvn first";
            return {};
        }

        nc::NdArray<float> audioNdArray = FloatVectorToNdArray(audioSample);
        nc::NdArray<float> scaledAudio = audioNdArray * static_cast<float>(1 << 15);

        nc::NdArray<float> fbankFeature =
            Fbank(scaledAudio, 0.42f, 0.0f, 25.0f, 10.0f, 0.0f, 20.0f, 80, 0.97f, true, true, 16000.0f, true, true,
                  true, -500.0f, 100.0f, 1.0f, WindowType::HAMMING);

        if (fbankFeature.isempty()) {
            SLOG_ERROR << "AudioFeature: Failed to compute FBank feature for ASR";
            return {};
        }

        nc::NdArray<float> lfrFbankFeature = ApplyLfr(fbankFeature, 7, 6);
        nc::NdArray<float> result = ApplyCmvn(lfrFbankFeature, "asr");

        return NdArrayToFloatMatrix(result);
    }

    std::vector<std::vector<float>> AudioFeature::VpFeature(const std::vector<float>& audioSample) {
        if (audioSample.empty()) {
            SLOG_ERROR << "AudioFeature: Empty input audio sample for VP feature extraction";
            return {};
        }

        nc::NdArray<float> audioNdArray = FloatVectorToNdArray(audioSample);

        nc::NdArray<float> feature = Fbank(audioNdArray, 0.42f, 0.0f, 25.0f, 10.0f, 0.0f, 20.0f, 80, 0.97f, true, true,
                                           16000.0f, true, true, true, -500.0f, 100.0f, 1.0f, WindowType::POVEY);

        if (feature.isempty()) {
            SLOG_ERROR << "AudioFeature: Failed to compute FBank feature for VP";
            return {};
        }

        nc::NdArray<double> meanDouble = nc::mean(feature, nc::Axis::ROW);
        nc::NdArray<float> mean(meanDouble.shape());
        for (size_t i = 0; i < mean.size(); ++i) {
            mean[i] = static_cast<float>(meanDouble[i]);
        }

        nc::NdArray<float> result = feature - mean;

        return NdArrayToFloatMatrix(result);
    }

    std::vector<std::vector<float>> AudioFeature::NdArrayToFloatMatrix(const nc::NdArray<float>& ndArray) {
        if (ndArray.isempty()) {
            return {};
        }

        size_t rows = ndArray.shape().rows;
        size_t cols = ndArray.shape().cols;

        std::vector<std::vector<float>> matrix(rows, std::vector<float>(cols));

        for (size_t i = 0; i < rows; ++i) {
            for (size_t j = 0; j < cols; ++j) {
                matrix[i][j] = ndArray(i, j);
            }
        }

        return matrix;
    }

    nc::NdArray<float> AudioFeature::FloatVectorToNdArray(const std::vector<float>& vec) {
        if (vec.empty()) {
            return nc::NdArray<float>();
        }

        nc::NdArray<float> ndArray(vec.size(), 1);

        for (size_t i = 0; i < vec.size(); ++i) {
            ndArray(i, 0) = vec[i];
        }

        return ndArray;
    }

}  // namespace qifeng
