#ifndef QIFENG_FRAMEWORK_ASR_MODEL_PRIVATE_INCLUDE_AUDIO_DECODER_H
#define QIFENG_FRAMEWORK_ASR_MODEL_PRIVATE_INCLUDE_AUDIO_DECODER_H

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/channel_layout.h"
#include "libavutil/error.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"
#include "libswresample/swresample.h"
}

#include "common/config_define.h"

namespace qifeng {
    namespace asr {

        constexpr int64_t CHANNELS = Aas::DefaultChannels;
        constexpr int64_t SAMPLE_RATE = Aas::DefaultSampleRate;

        constexpr std::size_t BITS_PER_SAMPLE = Aas::DefaultBitDepth;
        constexpr std::size_t BITS_PER_BYTE = 8;
        constexpr std::size_t BYTES_PER_SAMPLE = (BITS_PER_SAMPLE / BITS_PER_BYTE);

    }  // namespace asr
}  // namespace qifeng

#endif
