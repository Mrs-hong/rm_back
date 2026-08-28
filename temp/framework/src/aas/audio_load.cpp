/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unistd.h>

#include "aas/audio_load.h"
#include "common/logger.h"

namespace {

    // 内存监控辅助函数
    void LogMemoryUsage(const std::string& context) {
        std::ifstream statm("/proc/self/statm");
        if (statm.is_open()) {
            unsigned long size = 0;
            unsigned long resident = 0;
            unsigned long share = 0;
            unsigned long text = 0;
            unsigned long lib = 0;
            unsigned long data = 0;
            unsigned long dt = 0;
            statm >> size >> resident >> share >> text >> lib >> data >> dt;
            statm.close();

            unsigned long pageSize = static_cast<unsigned long>(sysconf(_SC_PAGESIZE));
            unsigned long rss = resident * pageSize / 1024;  // KB
            unsigned long vmSize = size * pageSize / 1024;   // KB

            SLOG_INFO << "内存使用 [" << context << "]: RSS=" << rss << "KB, VmSize=" << vmSize << "KB";
        }
    }

    // 初始化FFmpeg库
    void InitFFmpeg() {
        static bool Initialized = false;
        if (!Initialized) {
            avformat_network_init();
            Initialized = true;
        }
    }

    struct FFmpegDecodeContext {
        AVFormatContext* formatCtx = nullptr;
        AVCodecContext* codecCtx = nullptr;
        SwrContext* swrCtx = nullptr;
    };

    void CleanupFFmpegResources(FFmpegDecodeContext& ctx) {
        if (ctx.swrCtx) {
            swr_free(&ctx.swrCtx);
            ctx.swrCtx = nullptr;
        }

        if (ctx.codecCtx) {
            avcodec_flush_buffers(ctx.codecCtx);
            avcodec_free_context(&ctx.codecCtx);
            ctx.codecCtx = nullptr;
        }

        if (ctx.formatCtx) {
            avformat_close_input(&ctx.formatCtx);
            ctx.formatCtx = nullptr;
        }
    }

    // 强制清理FFmpeg全局缓存
    void ForceCleanupFFmpegGlobalCache() {
        // 清理FFmpeg全局缓存
        avformat_network_deinit();

        // 重新初始化网络（如果需要继续使用）
        avformat_network_init();

        SLOG_DEBUG << "FFmpeg全局缓存已清理";
    }

    // 获取音频流索引
    int GetAudioStreamIndex(AVFormatContext* formatCtx) {
        for (unsigned int i = 0; i < formatCtx->nb_streams; ++i) {
            if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    // 创建重采样器
    SwrContext* CreateResampler(AVCodecContext* codecCtx, int targetSampleRate) {
        SwrContext* swrCtx = swr_alloc();
        if (!swrCtx) {
            return nullptr;
        }

        // 设置输入参数
        av_opt_set_int(swrCtx, "in_channel_layout", av_get_default_channel_layout(codecCtx->channels), 0);
        av_opt_set_int(swrCtx, "in_sample_rate", codecCtx->sample_rate, 0);
        av_opt_set_sample_fmt(swrCtx, "in_sample_fmt", codecCtx->sample_fmt, 0);

        // 设置输出参数
        av_opt_set_int(swrCtx, "out_channel_layout", AV_CH_LAYOUT_MONO, 0);
        av_opt_set_int(swrCtx, "out_sample_rate", targetSampleRate, 0);
        av_opt_set_sample_fmt(swrCtx, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);

        if (swr_init(swrCtx) < 0) {
            swr_free(&swrCtx);
            return nullptr;
        }

        return swrCtx;
    }

    struct SeekResult {
        int streamIndex = -1;
        int64_t endTimestamp = 0;
    };

    bool SeekAudioStream(FFmpegDecodeContext& ctx, int64_t startTimeMs, int64_t durationMs, SeekResult& result) {
        result.streamIndex = GetAudioStreamIndex(ctx.formatCtx);
        if (result.streamIndex < 0) {
            SLOG_ERROR << "未找到音频流";
            return false;
        }

        AVRational timeBase = ctx.formatCtx->streams[result.streamIndex]->time_base;
        int64_t startTimestamp = av_rescale_q(startTimeMs * 1000, AV_TIME_BASE_Q, timeBase);

        if (av_seek_frame(ctx.formatCtx, result.streamIndex, startTimestamp, AVSEEK_FLAG_BACKWARD) < 0) {
            return false;
        }

        int64_t endTimeMs = startTimeMs + durationMs;
        result.endTimestamp = av_rescale_q(endTimeMs * 1000, AV_TIME_BASE_Q, timeBase);
        return true;
    }

    void ResampleDecodedFrame(FFmpegDecodeContext& ctx, AVFrame* frame, std::vector<float>& outAudioData) {
        int64_t outSamples = av_rescale_rnd(swr_get_delay(ctx.swrCtx, ctx.codecCtx->sample_rate) + frame->nb_samples,
                                            ctx.codecCtx->sample_rate, ctx.codecCtx->sample_rate, AV_ROUND_UP);

        std::vector<float> resampledData(static_cast<size_t>(outSamples));
        uint8_t* outData = reinterpret_cast<uint8_t*>(resampledData.data());

        int convertedSamples = swr_convert(ctx.swrCtx, &outData, static_cast<int>(outSamples),
                                           const_cast<const uint8_t**>(frame->data), frame->nb_samples);

        if (convertedSamples > 0) {
            outAudioData.insert(outAudioData.end(), resampledData.begin(), resampledData.begin() + convertedSamples);
        }

        av_frame_unref(frame);
    }

    void DecodePacketFrames(FFmpegDecodeContext& ctx, AVFrame* frame, std::vector<float>& outAudioData) {
        while (true) {
            int receiveResult = avcodec_receive_frame(ctx.codecCtx, frame);
            if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
                break;
            }
            if (receiveResult < 0) {
                SLOG_WARN << "音频解码错误: " << receiveResult;
                break;
            }
            ResampleDecodedFrame(ctx, frame, outAudioData);
        }
    }

    bool ReadAudioData(FFmpegDecodeContext& ctx, int64_t startTimeMs, int64_t durationMs,
                       std::vector<float>& outAudioData) {
        outAudioData.clear();
        outAudioData.reserve(4 * 1024 * 1024 / sizeof(float));

        SeekResult seekResult;
        if (!SeekAudioStream(ctx, startTimeMs, durationMs, seekResult)) {
            return false;
        }

        AVPacket* packet = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        if (!packet || !frame) {
            if (packet)
                av_packet_free(&packet);
            if (frame)
                av_frame_free(&frame);
            return false;
        }

        while (av_read_frame(ctx.formatCtx, packet) >= 0) {
            if (packet->stream_index != seekResult.streamIndex) {
                av_packet_unref(packet);
                continue;
            }

            if (packet->pts > seekResult.endTimestamp) {
                av_packet_unref(packet);
                break;
            }

            int sendResult = avcodec_send_packet(ctx.codecCtx, packet);
            av_packet_unref(packet);

            if (sendResult >= 0) {
                DecodePacketFrames(ctx, frame, outAudioData);
            }
        }

        av_packet_free(&packet);
        av_frame_free(&frame);
        return !outAudioData.empty();
    }

    bool OpenAudioDecoder(FFmpegDecodeContext& ctx, const std::string& wavFile, int sampleRate) {
        if (avformat_open_input(&ctx.formatCtx, wavFile.c_str(), nullptr, nullptr) != 0) {
            SLOG_ERROR << "无法打开音频文件: " << wavFile;
            return false;
        }

        if (avformat_find_stream_info(ctx.formatCtx, nullptr) < 0) {
            SLOG_ERROR << "无法获取音频流信息: " << wavFile;
            return false;
        }

        int audioStreamIndex = GetAudioStreamIndex(ctx.formatCtx);
        if (audioStreamIndex < 0) {
            SLOG_ERROR << "未找到音频流: " << wavFile;
            return false;
        }

        AVCodecParameters* codecParams = ctx.formatCtx->streams[audioStreamIndex]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
        if (!codec) {
            SLOG_ERROR << "未找到解码器: " << wavFile;
            return false;
        }

        ctx.codecCtx = avcodec_alloc_context3(codec);
        if (!ctx.codecCtx) {
            SLOG_ERROR << "无法创建解码器上下文: " << wavFile;
            return false;
        }

        if (avcodec_parameters_to_context(ctx.codecCtx, codecParams) < 0) {
            SLOG_ERROR << "无法复制编解码器参数到解码器上下文: " << wavFile;
            return false;
        }

        if (avcodec_open2(ctx.codecCtx, codec, nullptr) < 0) {
            SLOG_ERROR << "无法打开解码器: " << wavFile;
            return false;
        }

        ctx.swrCtx = CreateResampler(ctx.codecCtx, sampleRate);
        if (!ctx.swrCtx) {
            SLOG_ERROR << "无法创建重采样器: " << wavFile;
            return false;
        }

        return true;
    }

}  // namespace

std::vector<uint8_t> AudioLoad::GetPCMData(const qifeng::aas::Request& request, qifeng::aas::FormatConfig dst) {
    if (std::holds_alternative<qifeng::aas::AudioFile>(request)) {
        return ProcessAudioFile(std::get<qifeng::aas::AudioFile>(request), dst);
    }
    return ProcessPcmData(std::get<qifeng::aas::PcmData>(request), dst);
}

// ===== GetPCMData 辅助函数 =====
void AudioLoad::ResampleFirst(ResampleState& state, const uint8_t** inputData, int inputSamples) {
    int64_t estimateSamples = ::swr_get_out_samples(state.swrCtx, 0);
    if (estimateSamples < 0) {
        SLOG_ERROR << "Audio data resample error: swr_get_out_samples failed, code=" << estimateSamples;
    } else if (estimateSamples == 0) {
        estimateSamples = 1;  // 如果为0，也尝试重采样一次
    }
    // result 按 (累计采样点数 × CHANNELS) 个 float 管理，确保写入区间 [totalSamples,
    // totalSamples+estimateSamples) 不越界
    int64_t requiredFloats = (estimateSamples + state.totalSamples) * state.dst.mChannels * state.dst.mBitDepth / 8;
    if (requiredFloats > static_cast<int64_t>(state.result.size())) {
        state.result.resize(static_cast<size_t>(requiredFloats));
    }
    uint8_t* outData = reinterpret_cast<uint8_t*>(state.result.data()) +
                       state.totalSamples * state.dst.mChannels * state.dst.mBitDepth / 8;
    int convertedSamples =
        ::swr_convert(state.swrCtx, &outData, static_cast<int>(estimateSamples), inputData, inputSamples);
    if (convertedSamples < 0) {
        int code = convertedSamples;
        SLOG_ERROR << "Audio data resample error: swr_convert failed, code=" << code;
    }
    state.totalSamples = state.totalSamples + convertedSamples;
}

bool AudioLoad::ResampleFlush(ResampleState& state) {
    int64_t estimateSamples = ::swr_get_out_samples(state.swrCtx, 0);
    if (estimateSamples < 0) {
        SLOG_ERROR << "Audio data resample error: swr_get_out_samples failed, code=" << estimateSamples;
    } else if (estimateSamples == 0) {
        estimateSamples = 1;  // 如果为0，也尝试重采样一次
    }
    int64_t requiredFloats = (estimateSamples + state.totalSamples) * state.dst.mChannels * state.dst.mBitDepth / 8;
    if (requiredFloats > static_cast<int64_t>(state.result.size())) {
        state.result.resize(static_cast<size_t>(requiredFloats));
    }
    uint8_t* outData = reinterpret_cast<uint8_t*>(state.result.data()) +
                       state.totalSamples * state.dst.mChannels * state.dst.mBitDepth / 8;
    int convertedSamples = ::swr_convert(state.swrCtx, &outData, static_cast<int>(estimateSamples), nullptr, 0);
    if (convertedSamples < 0) {
        int code = convertedSamples;
        SLOG_ERROR << "Audio data resample error: swr_convert failed, code=" << code;
    } else if (convertedSamples == 0) {
        state.totalSamples += convertedSamples;
        state.result.resize(static_cast<size_t>(state.totalSamples) * static_cast<size_t>(state.dst.mChannels) *
                            static_cast<size_t>(state.dst.mBitDepth) / 8);
        return false;  // 冲刷完成
    } else {
        state.totalSamples += convertedSamples;
        return true;
    }
    return false;
}

void AudioLoad::CleanupFFmpegResource(FFmpegResource& res) {
    if (res.frame != nullptr) {
        ::av_frame_unref(res.frame);
        ::av_frame_free(&res.frame);
        res.frame = nullptr;
    }
    if (res.packet != nullptr) {
        ::av_packet_unref(res.packet);
        ::av_packet_free(&res.packet);
        res.packet = nullptr;
    }
    if (res.swrCtx != nullptr) {
        ::swr_free(&res.swrCtx);
        res.swrCtx = nullptr;
    }
    if (res.codecCtx != nullptr) {
        ::avcodec_free_context(&res.codecCtx);
        res.codecCtx = nullptr;
    }
    if (res.fmtCtx != nullptr) {
        ::avformat_close_input(&res.fmtCtx);
        res.fmtCtx = nullptr;
    }
}

void AudioLoad::OpenAudioFileAndDecoder(FFmpegResource& res, const std::string& path, unsigned int& audioStreamIndex) {
    if (int code = ::avformat_open_input(&res.fmtCtx, path.c_str(), nullptr, nullptr); code < 0) {
        throw std::runtime_error {"Audio file extract error: avformat_open_input failed, code=" + std::to_string(code)};
    }
    if (int code = ::avformat_find_stream_info(res.fmtCtx, nullptr); code < 0) {
        throw std::runtime_error {"Audio file extract error: avformat_find_stream_info failed, code=" +
                                  std::to_string(code)};
    }
    int audioStreamCount {0};
    for (unsigned int i = 0; i < res.fmtCtx->nb_streams; i++) {
        if (res.fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioStreamIndex = i;
            audioStreamCount++;
        }
    }
    if (audioStreamCount != 1) {
        if (audioStreamCount == 0) {
            throw std::runtime_error {"Audio file extract error: audio stream not found."};
        }
        throw std::runtime_error {"Audio file extract error: multiple audio streams is not supported."};
    }
    const AVCodec* codec = ::avcodec_find_decoder(res.fmtCtx->streams[audioStreamIndex]->codecpar->codec_id);
    if (codec == nullptr) {
        throw std::runtime_error {"Audio file extract error: available decoder not found."};
    }
    res.codecCtx = ::avcodec_alloc_context3(codec);
    if (res.codecCtx == nullptr) {
        throw std::runtime_error {"Audio file extract error: decoder context init failed."};
    }
    if (int code = ::avcodec_parameters_to_context(res.codecCtx, res.fmtCtx->streams[audioStreamIndex]->codecpar);
        code < 0) {
        SLOG_ERROR << "Audio file extract error: avcodec_parameters_to_context failed, code=" << code;
    }
    if (int code = ::avcodec_open2(res.codecCtx, codec, nullptr); code < 0) {
        SLOG_ERROR << "Audio file extract error: avcodec_open2 failed, code=" << code;
    }
}

void AudioLoad::InitAudioResampler(FFmpegResource& res, qifeng::aas::FormatConfig dst) {
    res.swrCtx = ::swr_alloc();
    AVSampleFormat dstFmt = GetDstSampleFormat(dst.mBitDepth);
    if (res.swrCtx == nullptr) {
        throw std::runtime_error {"Audio data extract error: swr_alloc failed."};
    }
    ::av_opt_set_int(res.swrCtx, "in_channel_count", res.codecCtx->channels, 0);
    ::av_opt_set_int(res.swrCtx, "in_sample_rate", res.codecCtx->sample_rate, 0);
    ::av_opt_set_sample_fmt(res.swrCtx, "in_sample_fmt", res.codecCtx->sample_fmt, 0);
    ::av_opt_set_int(res.swrCtx, "out_channel_layout", ::av_get_default_channel_layout(dst.mChannels), 0);
    ::av_opt_set_int(res.swrCtx, "out_sample_rate", dst.mSampleRate, 0);
    ::av_opt_set_sample_fmt(res.swrCtx, "out_sample_fmt", dstFmt, 0);
    if (int code = ::swr_init(res.swrCtx); code < 0) {
        SLOG_ERROR << "Audio data extract error: swr_init failed, code=" << code;
    }
}

void AudioLoad::DecodeAudioStream(FFmpegResource& res, std::vector<uint8_t>& result, unsigned int audioStreamIndex,
                                  qifeng::aas::FormatConfig dst) {
    int64_t totalSamples {0};
    ResampleState state {res.swrCtx, totalSamples, result, dst};
    while (true) {
        if (int code = ::av_read_frame(res.fmtCtx, res.packet); code < 0) {
            if (code == AVERROR_EOF) {
                break;
            }
            SLOG_ERROR << "Audio data extract error: av_read_frame failed, code=" << code;
        }
        if (res.packet->stream_index != static_cast<int>(audioStreamIndex)) {
            ::av_packet_unref(res.packet);
            continue;
        }
        if (int code = ::avcodec_send_packet(res.codecCtx, res.packet);
            code < 0 && code != AVERROR_EOF && code != AVERROR(EAGAIN)) {
            SLOG_ERROR << "Audio data extract error: avcodec_send_packet failed, code=" << code;
        }
        ::av_packet_unref(res.packet);
        while (true) {
            if (int code = ::avcodec_receive_frame(res.codecCtx, res.frame); code < 0) {
                if (code == AVERROR_EOF || code == AVERROR(EAGAIN)) {
                    break;
                }
                SLOG_ERROR << "Audio data extract error: avcodec_receive_frame failed, code=" << code;
            }
            ResampleFirst(state, const_cast<const uint8_t**>(res.frame->data), res.frame->nb_samples);
            do {
                if (!ResampleFlush(state)) {
                    break;
                }
            } while (true);
            ::av_frame_unref(res.frame);
        }
    }
    result.resize(static_cast<size_t>(totalSamples) * static_cast<size_t>(dst.mChannels));
}

std::vector<uint8_t> AudioLoad::ProcessAudioFile(const qifeng::aas::AudioFile& audioFile,
                                                 qifeng::aas::FormatConfig dst) {
    FFmpegResource res;
    std::vector<uint8_t> result;
    unsigned int audioStreamIndex {0};
    try {
        OpenAudioFileAndDecoder(res, audioFile.path.string(), audioStreamIndex);
        InitAudioResampler(res, dst);
        res.packet = ::av_packet_alloc();
        res.frame = ::av_frame_alloc();
        if (res.packet == nullptr || res.frame == nullptr) {
            throw std::runtime_error {"Audio data extract error: alloc packet/frame failed."};
        }
        DecodeAudioStream(res, result, audioStreamIndex, dst);
    } catch (...) {
        CleanupFFmpegResource(res);
        throw;
    }
    CleanupFFmpegResource(res);
    return result;
}

AVSampleFormat AudioLoad::GetSrcSampleFormat(uint16_t bitDepth) {
    switch (bitDepth) {
        case 16:
            return AV_SAMPLE_FMT_S16;
        case 32:
            return AV_SAMPLE_FMT_FLT;
        default:
            throw std::runtime_error {"SRC Audio data validation error: bitDepth can only be 16 and 32."};
    }
}

AVSampleFormat AudioLoad::GetDstSampleFormat(uint16_t bitDepth) {
    switch (bitDepth) {
        case 16:
            return AV_SAMPLE_FMT_S16;
        case 32:
            return AV_SAMPLE_FMT_FLT;
        default:
            throw std::runtime_error {"DST Audio data validation error: bitDepth can only be 16 and 32."};
    }
}

void AudioLoad::ValidatePcmData(const qifeng::aas::PcmData& pcmData, qifeng::aas::FormatConfig dst) {
    if (pcmData.size == 0) {
        throw std::runtime_error {"Audio data validation error: pcm can not be empty."};
    }
    if (pcmData.sampleRate == 0) {
        throw std::runtime_error {"Audio data validation error: sampleRate can not be zero."};
    }
    if ((pcmData.size % (dst.mChannels * pcmData.bitDepth / 8)) != 0) {
        throw std::runtime_error {"Audio data validation error: pcm data is not well-aligned."};
    }
}

std::vector<uint8_t> AudioLoad::ResamplePcmInMemory(const qifeng::aas::PcmData& pcmData, AVSampleFormat srcFmt,
                                                    qifeng::aas::FormatConfig dst, std::size_t pcmSamples) {
    SwrContext* swrCtx {nullptr};
    std::vector<uint8_t> result;
    AVSampleFormat dstFmt = GetDstSampleFormat(dst.mBitDepth);
    try {
        swrCtx = ::swr_alloc();
        if (swrCtx == nullptr) {
            throw std::runtime_error {"Audio data resample error: swr_alloc failed."};
        }
        ::av_opt_set_int(swrCtx, "in_channel_layout", ::av_get_default_channel_layout(pcmData.channels), 0);
        ::av_opt_set_int(swrCtx, "in_sample_rate", pcmData.sampleRate, 0);
        ::av_opt_set_sample_fmt(swrCtx, "in_sample_fmt", srcFmt, 0);
        ::av_opt_set_int(swrCtx, "out_channel_layout", ::av_get_default_channel_layout(dst.mChannels), 0);
        ::av_opt_set_int(swrCtx, "out_sample_rate", dst.mSampleRate, 0);
        ::av_opt_set_sample_fmt(swrCtx, "out_sample_fmt", dstFmt, 0);
        if (int code = ::swr_init(swrCtx); code < 0) {
            SLOG_ERROR << "Audio data resample error: swr_init failed, code=" << code;
        }
        int64_t totalSamples {0};
        ResampleState state {swrCtx, totalSamples, result, dst};
        const uint8_t* pcmRawData = pcmData.raw;
        ResampleFirst(state, const_cast<const uint8_t**>(&pcmRawData), static_cast<int>(pcmSamples));
        do {
            if (!ResampleFlush(state)) {
                break;
            }
        } while (true);
    } catch (...) {
        if (swrCtx != nullptr) {
            ::swr_free(&swrCtx);
        }
        throw;
    }
    if (swrCtx != nullptr) {
        ::swr_free(&swrCtx);
    }
    return result;
}

std::vector<uint8_t> AudioLoad::ProcessPcmData(const qifeng::aas::PcmData& pcmData, qifeng::aas::FormatConfig dst) {
    ValidatePcmData(pcmData, dst);
    AVSampleFormat srcFmt = GetSrcSampleFormat(pcmData.bitDepth);
    std::size_t pcmSamples = static_cast<std::size_t>(pcmData.size) / (static_cast<std::size_t>(pcmData.channels) *
                                                                       static_cast<std::size_t>(pcmData.bitDepth) / 8);
    if (pcmData.sampleRate == dst.mSampleRate && pcmData.channels == dst.mChannels &&
        pcmData.bitDepth == dst.mBitDepth) {
        std::vector<uint8_t> result(pcmSamples * dst.mChannels * dst.mBitDepth / 8);
        std::memcpy(result.data(), pcmData.raw, static_cast<std::size_t>(pcmData.size));
        return result;
    }
    return ResamplePcmInMemory(pcmData, srcFmt, dst, pcmSamples);
}

std::vector<float> AudioLoad::LoadAudio(const std::string& wavFile, int offset, const int* duration, int sampleRate) {
    try {
        SLOG_DEBUG << "开始加载音频文件: " << wavFile << ", offset=" << offset << "ms"
                   << ", sampleRate=" << sampleRate
                   << ", duration=" << (duration ? std::to_string(*duration) + "ms" : "null");

        std::vector<float> audioData;
        int ret = LoadAudioWithFFmpeg(wavFile, {offset, duration, sampleRate}, audioData);

        if (ret != 0) {
            SLOG_ERROR << "加载音频文件失败: " << wavFile << ", errorCode=" << ret;
            return {};
        }

        SLOG_DEBUG << "音频文件加载成功: " << wavFile << ", samples=" << audioData.size();
        return audioData;
    } catch (const std::exception& e) {
        SLOG_ERROR << "加载音频文件异常: " << wavFile << ", errorCode=" << e.what();
        return {};
    }
}

int AudioLoad::LoadAudioWithFFmpeg(const std::string& wavFile, AudioLoadConfig config,
                                   std::vector<float>& outAudioData) {
    InitFFmpeg();
    LogMemoryUsage("AudioLoad开始前");

    FFmpegDecodeContext ctx;

    try {
        if (!OpenAudioDecoder(ctx, wavFile, config.sampleRate)) {
            CleanupFFmpegResources(ctx);
            return -1;
        }

        int64_t durationMs = -1;
        if (config.duration != nullptr && *config.duration > 0) {
            durationMs = *config.duration;
        }

        bool readSuccess = ReadAudioData(ctx, config.offset, durationMs, outAudioData);
        if (!readSuccess) {
            SLOG_ERROR << "读取音频数据失败: " << wavFile;
        }

        CleanupFFmpegResources(ctx);
        LogMemoryUsage("AudioLoad完成后");

        return 0;
    } catch (const std::exception& e) {
        CleanupFFmpegResources(ctx);
        SLOG_ERROR << "FFmpeg加载音频失败: " << wavFile << ", errorCode=" << e.what();
        return -1;
    }
}

// 强制清理FFmpeg全局缓存
void AudioLoadUtils::ForceCleanupFFmpegCache() {
    ForceCleanupFFmpegGlobalCache();
}

int AudioLoad::LoadAudioMeta(const std::string& wavFile, std::array<int, 2>& outMetaData) {
    try {
        SLOG_DEBUG << "开始获取音频元数据: " << wavFile;

        int sampleRate = 0;
        int durationMs = 0;

        LoadAudioMetaWithFFmpeg(wavFile, sampleRate, durationMs);

        // 设置输出参数
        outMetaData[0] = sampleRate;
        outMetaData[1] = durationMs;

        SLOG_DEBUG << "音频元数据获取成功: " << wavFile << ", sampleRate=" << sampleRate
                   << ", durationMs=" << durationMs;

        return 0;
    } catch (const std::exception& e) {
        SLOG_ERROR << "获取音频元数据异常: " << wavFile << ", errorCode=" << e.what();

        return -1;
    }
}

int AudioLoad::LoadAudioMetaWithFFmpeg(const std::string& wavFile, int& outSampleRate, int& outDurationMs) {
    InitFFmpeg();
    AVFormatContext* formatCtx = nullptr;
    try {
        if (avformat_open_input(&formatCtx, wavFile.c_str(), nullptr, nullptr) != 0) {  // 打开音频文件
            SLOG_ERROR << "无法打开音频文件: " << wavFile;
        }
        if (avformat_find_stream_info(formatCtx, nullptr) < 0) {  // 获取流信息
            SLOG_ERROR << "无法获取音频流信息: " << wavFile;
        }
        int audioStreamIndex = GetAudioStreamIndex(formatCtx);  // 获取音频流索引
        if (audioStreamIndex < 0) {
            SLOG_ERROR << "未找到音频流: " << wavFile;
        }
        AVStream* audioStream = formatCtx->streams[audioStreamIndex];  // 获取音频流
        if (!audioStream) {
            SLOG_ERROR << "未找到音频流: " << wavFile;
        }
        AVCodecParameters* codecParams = audioStream->codecpar;  // 获取编解码器参数
        if (!codecParams) {
            SLOG_ERROR << "未找到音频流的编解码器参数: " << wavFile;
        }
        outSampleRate = codecParams->sample_rate;  // 获取采样率
        if (outSampleRate <= 0) {
            SLOG_ERROR << "无效的采样率: " << outSampleRate << " for " << wavFile;
        }
        if (audioStream->duration != AV_NOPTS_VALUE) {  // 计算时长（毫秒）
            // 使用流时长计算
            AVRational timeBase = audioStream->time_base;
            double durationSeconds = static_cast<double>(audioStream->duration) * timeBase.num / timeBase.den;
            outDurationMs = static_cast<int>(durationSeconds * 1000);
        } else if (formatCtx->duration != AV_NOPTS_VALUE) {
            // 使用容器时长计算
            double durationSeconds = static_cast<double>(formatCtx->duration) / AV_TIME_BASE;
            outDurationMs = static_cast<int>(durationSeconds * 1000);
        } else {
            outDurationMs = -1;  // 无法获取时长
        }
        avformat_close_input(&formatCtx);  // 关闭文件
        return 0;
    } catch (const std::exception& e) {
        if (formatCtx != nullptr) {  // 清理资源
            avformat_close_input(&formatCtx);
        }
        SLOG_ERROR << "FFmpeg获取音频元数据失败: " << wavFile << ", errorCode=" << e.what();
        outSampleRate = -1;
        outDurationMs = -1;
        return -1;
    }
}
