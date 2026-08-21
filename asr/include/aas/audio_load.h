/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_AAS_AUDIOLOAD_H
#define QIFENG_FRAMEWORK_AAS_AUDIOLOAD_H

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include "aas/aas_callback.h"

// FFmpeg头文件
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

struct SwrContext;

namespace qifeng {
    namespace aas {
        /**
         * @brief 获取PCM数据
         * @param request 模型请求，包含音频数据
         * @return 重采样后的PCM数据（float32格式）
         *
         * 从模型请求中提取PCM数据，并根据配置进行重采样
         */
        struct AudioFile {
            std::filesystem::path path;  // 语音文件路径
        };

        struct PcmData {
            uint8_t* raw = nullptr;   // 原始语音数据指针
            int32_t size = 0;         // 原始语音数据大小
            uint32_t sampleRate = 0;  // 采样率
            uint16_t channels = 0;    // 通道数
            uint16_t bitDepth = 0;    // 位深
        };
        using Request = std::variant<AudioFile, PcmData>;

    }  // namespace aas
}  // namespace qifeng

/**
 * @class AudioLoad
 * @brief 音频加载模块，提供与Python接口完全一致的C++实现
 *
 * 本模块提供与Python load_audio函数完全一致的接口，支持WAV格式音频文件的加载，
 * 支持指定偏移量和时长，支持重采样到目标采样率，支持多声道音频转换为单声道
 */
class AudioLoad {
public:
    /**
     * @brief 加载音频文件
     * @param wavFile 音频文件路径
     * @param offset 偏移量（毫秒），默认0
     * @param duration 加载时长（毫秒），默认nullptr表示加载到文件结束
     * @param sampleRate 目标采样率，默认16000
     * @return 音频数据向量（浮点数格式）
     *
     * 接口与Python load_audio函数完全一致：
     * - 支持指定偏移量（毫秒）
     * - 支持指定加载时长（毫秒）
     * - 支持重采样到目标采样率
     * - 多声道音频自动转换为单声道（取平均值）
     * - 如果加载失败，返回空向量
     */
    static std::vector<float> LoadAudio(const std::string& wavFile, int offset = 0, const int* duration = nullptr,
                                        int sampleRate = 16000);

    /**
     * @brief 获取音频元数据
     * @param wavFile 音频文件路径
     * @param outMetaData 输出元数据数组，包含采样率和毫秒数
     * @return 成功返回ERR_OK，失败返回错误码
     *
     * 接口与Python load_audio_meta函数对应：
     * - 获取音频文件的采样率
     * - 计算音频时长（毫秒）
     * - 通过数组返回结果
     */
    static int LoadAudioMeta(const std::string& wavFile, std::array<int, 2>& outMetaData);

    struct ResampleState {
        SwrContext* swrCtx = nullptr;
        int64_t& totalSamples;
        std::vector<uint8_t>& result;
        qifeng::aas::FormatConfig dst;
    };
    struct FFmpegResource {
        AVFrame* frame {nullptr};
        AVPacket* packet {nullptr};
        SwrContext* swrCtx {nullptr};
        AVCodecContext* codecCtx {nullptr};
        AVFormatContext* fmtCtx {nullptr};
    };

    static std::vector<uint8_t> GetPCMData(const qifeng::aas::Request& request, qifeng::aas::FormatConfig dst);

    // GetPCMData的辅助函数
    static void ResampleFirst(ResampleState& state, const uint8_t** inputData, int inputSamples);
    static bool ResampleFlush(ResampleState& state);
    static void CleanupFFmpegResource(FFmpegResource& res);
    static void OpenAudioFileAndDecoder(FFmpegResource& res, const std::string& path, unsigned int& audioStreamIndex);
    static void InitAudioResampler(FFmpegResource& res, qifeng::aas::FormatConfig dst);
    static void DecodeAudioStream(FFmpegResource& res, std::vector<uint8_t>& result, unsigned int audioStreamIndex,
                                  qifeng::aas::FormatConfig dst);
    static std::vector<uint8_t> ProcessAudioFile(const qifeng::aas::AudioFile& audioFile,
                                                 qifeng::aas::FormatConfig dst);
    static AVSampleFormat GetSrcSampleFormat(uint16_t bitDepth);
    static AVSampleFormat GetDstSampleFormat(uint16_t bitDepth);
    static void ValidatePcmData(const qifeng::aas::PcmData& pcmData, qifeng::aas::FormatConfig dst);
    static std::vector<uint8_t> ResamplePcmInMemory(const qifeng::aas::PcmData& pcmData, AVSampleFormat srcFmt,
                                                    qifeng::aas::FormatConfig dst, std::size_t pcmSamples);
    static std::vector<uint8_t> ProcessPcmData(const qifeng::aas::PcmData& pcmData, qifeng::aas::FormatConfig dst);

private:
    /**
     * @brief 使用FFmpeg加载音频文件
     * @param wavFile 音频文件路径
     * @param offset 偏移量（毫秒）
     * @param duration 加载时长（毫秒），nullptr表示加载到文件结束
     * @param sampleRate 目标采样率
     * @param outAudioData 输出音频数据
     * @return 成功返回0，失败返回错误码
     *
     * 使用FFmpeg库实现音频加载，支持多种音频格式
     */
    struct AudioLoadConfig {
        int offset = 0;
        const int* duration = nullptr;
        int sampleRate = 16000;
    };

    static int LoadAudioWithFFmpeg(const std::string& wavFile, AudioLoadConfig config,
                                   std::vector<float>& outAudioData);

    /**
     * @brief 使用FFmpeg获取音频元数据
     * @param wavFile 音频文件路径
     * @param outSampleRate 输出采样率
     * @param outDurationMs 输出时长（毫秒）
     * @return 成功返回0，失败返回错误码
     *
     * 使用FFmpeg库获取音频文件的元数据信息
     */
    static int LoadAudioMetaWithFFmpeg(const std::string& wavFile, int& outSampleRate, int& outDurationMs);
};

/**
 * @brief 音频加载工具函数
 *
 * 提供音频加载相关的工具函数
 */
namespace AudioLoadUtils {
    /**
     * @brief 强制清理FFmpeg全局缓存
     * @return 无
     *
     * 清理FFmpeg的全局缓存和网络资源，用于解决内存泄漏问题
     * 注意：调用此函数后，如果需要继续使用FFmpeg，需要重新初始化
     */
    void ForceCleanupFFmpegCache();
}  // namespace AudioLoadUtils

#endif  // QIFENG_FRAMEWORK_AAS_AUDIOLOAD_H
