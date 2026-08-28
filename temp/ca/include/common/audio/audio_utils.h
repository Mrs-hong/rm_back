//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_AUDIO_AUDIO_UTILS_H
#define QIFENG_CA_INCLUDE_COMMON_AUDIO_AUDIO_UTILS_H

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace qifeng_ca {

    struct AudioUtilsConfig {
        uint32_t mSampleRate {16000};
        uint16_t mChannels {1};
        uint16_t mBitDepth {16};
    };

    struct WavHeaderInfo {
        int mSampleRate {0};
        int mChannels {0};
        int mBitDepth {0};
        uint64_t mDataSize {0};
    };

    class AudioUtils {
    public:
        AudioUtils() = delete;

        static bool IsWavFormatMatch(const std::string &filePath, const AudioUtilsConfig &targetConfig);

        static bool ParseWavHeader(const std::string &filePath, WavHeaderInfo &outInfo);

        // 检查WAV是否为标准44字节头部(RIFF + 16字节fmt + data)
        // 32位浮点WAV(带fact chunk)、扩展fmt等非标准头部返回false
        static bool HasStandardWavHeader(const std::string &filePath);

        static void WriteWavHeader(std::ostream &outFile, const AudioUtilsConfig &config);

        static void FinalizeWavFile(std::ostream &outFile, uint64_t dataSize);

        // 相比GetWavHeaderInfo增加了文件大小验证
        static bool GetWavHeaderInfo(const std::string &filePath, WavHeaderInfo &outInfo);

        // 计算每秒字节数: sampleRate * channels * (bitDepth / 8)
        static size_t CalculateOneSecondBytes(const AudioUtilsConfig &config);

        // 计算每毫秒字节数: CalculateOneSecondBytes / 1000
        static size_t CalculateBytesPerMs(const AudioUtilsConfig &config);

        // 根据数据大小计算时长(ms): dataSize / bytesPerMs
        static int32_t CalculateDurationMs(size_t dataSize, const AudioUtilsConfig &config);

        static int32_t CalculateTotalTimeFromWav(const std::string &wavPath);

        static constexpr int WavHeaderSize = 44;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_AUDIO_AUDIO_UTILS_H
