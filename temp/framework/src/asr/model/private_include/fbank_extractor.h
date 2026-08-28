#ifndef QIFENG_FRAMEWORK_ASR_MODEL_PRIVATE_INCLUDE_FBANK_EXTRACTOR_H
#define QIFENG_FRAMEWORK_ASR_MODEL_PRIVATE_INCLUDE_FBANK_EXTRACTOR_H

#include <cstdint>
#include <memory>
#include <vector>

namespace qifeng {
    namespace asr {

        struct MelBinRange {
            int32_t start;
            int32_t end;
        };

        struct MelConfig {
            const std::vector<float>* matrix;
            const std::vector<MelBinRange>* ranges;
            uint32_t featureSize;
            uint32_t frameLength;
        };

        void AlignPcmToSegment(std::unique_ptr<std::vector<float>>& pcmData, int64_t maxAudioSeconds,
                               int64_t sampleRate);
        std::vector<float> ComputeMelSpectrogram(const float* pcmData, int32_t numSamples, uint32_t featureSize,
                                                 uint32_t frameLength, int64_t sampleRate);
        void PostProcessFbankFeatures(std::vector<float>& features);

    }  // namespace asr
}  // namespace qifeng

#endif
