//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_AUDIO_AUDIO_FILE_CONVERTER_H
#define QIFENG_CA_INCLUDE_COMMON_AUDIO_AUDIO_FILE_CONVERTER_H

#include <cstdint>
#include <string>

namespace qifeng_ca {

    // 目标 WAV 格式配置
    struct AudioTargetFormat {
        uint32_t mSampleRate {16000};
        uint16_t mChannels {1};
        uint16_t mBitDepth {16};
    };

    // 音频格式转换器接口(策略模式, 便于扩展新格式)
    class IAudioFormatConverter {
    public:
        virtual ~IAudioFormatConverter() = default;

        IAudioFormatConverter() = default;
        IAudioFormatConverter(const IAudioFormatConverter &) = delete;
        IAudioFormatConverter &operator=(const IAudioFormatConverter &) = delete;
        IAudioFormatConverter(IAudioFormatConverter &&) = delete;
        IAudioFormatConverter &operator=(IAudioFormatConverter &&) = delete;

        // 是否能处理该文件(按扩展名判断)
        virtual bool CanHandle(const std::string &srcPath) const = 0;

        // 将源文件转换为目标格式的WAV, 成功返回true
        virtual bool Convert(const std::string &srcPath, const std::string &dstPath,
                             const AudioTargetFormat &target) = 0;
    };

    // WAV 格式转换器: 格式匹配则拷贝, 不匹配则重采样
    class WavFormatConverter : public IAudioFormatConverter {
    public:
        bool CanHandle(const std::string &srcPath) const override;

        bool Convert(const std::string &srcPath, const std::string &dstPath, const AudioTargetFormat &target) override;
    };

    // MP3 格式转换器: 通过 AudioLoad::GetPCMData 解码重采样, 失败则回退到 ffmpeg
    class Mp3FormatConverter : public IAudioFormatConverter {
    public:
        bool CanHandle(const std::string &srcPath) const override;

        bool Convert(const std::string &srcPath, const std::string &dstPath, const AudioTargetFormat &target) override;
    };

    // 音频文件转换器(统一入口, 自动选择转换器)
    class AudioFileConverter {
    public:
        AudioFileConverter() = delete;

        // 将任意支持的音频文件转换为目标WAV格式
        // 自动根据文件类型选择合适的转换器
        static bool Convert(const std::string &srcPath, const std::string &dstPath,
                            const AudioTargetFormat &target = {});

        // 检查文件是否为支持的音频格式(WAV/MP3)
        static bool IsSupportedAudioFile(const std::string &srcPath);

        // 检查文件是否为WAV格式
        static bool IsWavFile(const std::string &srcPath);

        // 检查文件是否为MP3格式
        static bool IsMp3File(const std::string &srcPath);

        // 获取音频文件时长(秒), 失败返回 -1
        // 目前只有文件上传时调用，因为需要调用 ffprobe 获取时长效率不高，所以就入口校验
        static int64_t GetAudioDurationSeconds(const std::string &filePath);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_AUDIO_AUDIO_FILE_CONVERTER_H
