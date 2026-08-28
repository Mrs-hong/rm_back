//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "qifeng_framework/common/logger.h"

#include "aas/audio_load.h"
#include "common/audio/audio_file_converter.h"
#include "common/audio/audio_utils.h"
#include "common/utils/file_opt.h"

namespace qifeng_ca {

    // 获取小写文件扩展名(含点), 如 ".wav"
    static std::string GetLowerExtension(const std::string &path) {
        auto pos = path.rfind('.');
        if (pos == std::string::npos) {
            return {};
        }
        std::string ext = path.substr(pos);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return ext;
    }

    // Shell 单引号转义
    static std::string ShellQuote(const std::string &path) {
        std::string out = "'";
        for (char c : path) {
            if (c == '\'') {
                out += "'\\''";
            } else {
                out += c;
            }
        }
        out += "'";
        return out;
    }

    // 查找 ffmpeg 可执行文件: 优先 PATH, 其次 Sophon SDK 常见路径
    // Sophon 版 ffmpeg 通常不在系统 PATH 中, 需在 /opt/sophon/ 下查找
    static std::string FindFFmpegBinary() {
        FILE* pipe = ::popen("command -v ffmpeg 2>/dev/null", "r");
        if (pipe != nullptr) {
            std::array<char, 256> buf {};
            bool found = false;
            std::string path;
            if (fgets(buf.data(), buf.size(), pipe) != nullptr) {
                path = buf.data();
                while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) {
                    path.pop_back();
                }
                found = !path.empty() && std::filesystem::exists(path);
            }
            ::pclose(pipe);
            if (found) {
                return path;
            }
        }
        for (const auto* p : {
                 "/opt/sophon/sophon-ffmpeg-latest/bin/ffmpeg",
                 "/opt/sophon/libsophon-current/bin/ffmpeg",
                 "/opt/sophon/ffmpeg-current/bin/ffmpeg",
                 "/opt/sophon/ffmpeg/bin/ffmpeg",
                 "/usr/local/bin/ffmpeg",
                 "/usr/bin/ffmpeg",
             }) {
            if (std::filesystem::exists(p)) {
                return p;
            }
        }
        return {};
    }

    // 查找 ffprobe 可执行文件: 查找逻辑同 ffmpeg, 用于获取音频时长
    static std::string FindFFprobeBinary() {
        FILE* pipe = ::popen("command -v ffprobe 2>/dev/null", "r");
        if (pipe != nullptr) {
            std::array<char, 256> buf {};
            bool found = false;
            std::string path;
            if (fgets(buf.data(), buf.size(), pipe) != nullptr) {
                path = buf.data();
                while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) {
                    path.pop_back();
                }
                found = !path.empty() && std::filesystem::exists(path);
            }
            ::pclose(pipe);
            if (found) {
                return path;
            }
        }
        for (const auto* p : {
                 "/opt/sophon/sophon-ffmpeg-latest/bin/ffprobe",
                 "/opt/sophon/libsophon-current/bin/ffprobe",
                 "/opt/sophon/ffmpeg-current/bin/ffprobe",
                 "/opt/sophon/ffmpeg/bin/ffprobe",
                 "/usr/local/bin/ffprobe",
                 "/usr/bin/ffprobe",
             }) {
            if (std::filesystem::exists(p)) {
                return p;
            }
        }
        return {};
    }

    // 回退方案: 通过 ffmpeg 命令行转换 (ffmpeg 内部会正确冲刷解码器/重采样器, 不存在截断)
    static bool ConvertViaFFmpeg(const std::string &srcPath, const std::string &dstPath,
                                 const AudioTargetFormat &target) {
        std::string ffmpegBin = FindFFmpegBinary();
        if (ffmpegBin.empty()) {
            SLOG_ERROR << "ConvertViaFFmpeg: ffmpeg not found in PATH or /opt/sophon/*";
            return false;
        }
        if (!FileOpt::CreateDstDirectory(dstPath)) {
            return false;
        }
        // 注意: 清空 LD_LIBRARY_PATH, 避免父进程的 sophon 旧版 libavformat(58.20.100)
        // 覆盖系统库(58.76.100), 否则会触发 "undefined symbol: avio_print_string_array" 错误
        std::string cmd = "env LD_LIBRARY_PATH= " + ShellQuote(ffmpegBin) + " -y -i " + ShellQuote(srcPath) + " -ar " +
                          std::to_string(target.mSampleRate) + " -ac " + std::to_string(target.mChannels) +
                          " -sample_fmt s16 " + ShellQuote(dstPath) + " 2>&1";
        SLOG_INFO << "ConvertViaFFmpeg: " << cmd;
        FILE* pipe = popen(cmd.c_str(), "r");
        if (pipe == nullptr) {
            SLOG_ERROR << "ConvertViaFFmpeg: popen failed";
            return false;
        }
        std::string output;
        std::array<char, 256> buffer {};
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            output += buffer.data();
        }
        int code = pclose(pipe);
        if (code != 0) {
            SLOG_ERROR << "ConvertViaFFmpeg: failed, code=" << code << ", output=" << output;
            return false;
        }
        return std::filesystem::exists(dstPath) && std::filesystem::file_size(dstPath) > 0;
    }

    // 转换策略: 优先 ffmpeg 命令行(内部正确冲刷, 无截断), 失败则回退 AudioLoad
    static bool ConvertWithFallback(const std::string &srcPath, const std::string &dstPath,
                                    const AudioTargetFormat &target) {
        if (ConvertViaFFmpeg(srcPath, dstPath, target)) {
            SLOG_INFO << "ConvertWithFallback: ffmpeg success, src=" << srcPath << " dst=" << dstPath;
            return true;
        }
        return false;
    }

    // WavFormatConverter
    bool WavFormatConverter::CanHandle(const std::string &srcPath) const {
        return GetLowerExtension(srcPath) == ".wav";
    }

    bool WavFormatConverter::Convert(const std::string &srcPath, const std::string &dstPath,
                                     const AudioTargetFormat &target) {
        if (srcPath == dstPath) {
            return true;
        }
        if (!FileOpt::CreateDstDirectory(dstPath)) {
            return false;
        }
        // 仅当标准44字节头且16位深时直接rename保持原格式不变, 其余(非标准头或非16位深)均重采样
        WavHeaderInfo headerInfo;
        bool isStandard16 = AudioUtils::HasStandardWavHeader(srcPath) &&
                            AudioUtils::ParseWavHeader(srcPath, headerInfo) && headerInfo.mBitDepth == 16;
        if (isStandard16) {
            std::error_code ec;
            std::filesystem::rename(srcPath, dstPath, ec);
            if (ec) {
                SLOG_ERROR << "WavFormatConverter: rename failed, ec=" << ec.message();
                return false;
            }
            return true;
        }
        // 非标准头部(如32位浮点WAV带fact chunk、扩展fmt等)或非16位深: 转成默认格式(标准44字节头WAV)
        SLOG_INFO << "WavFormatConverter: non-standard or non-16bit WAV, converting, src=" << srcPath
                  << " bitDepth=" << headerInfo.mBitDepth;
        return ConvertWithFallback(srcPath, dstPath, target);
    }

    // Mp3FormatConverter
    bool Mp3FormatConverter::CanHandle(const std::string &srcPath) const {
        return GetLowerExtension(srcPath) == ".mp3";
    }

    bool Mp3FormatConverter::Convert(const std::string &srcPath, const std::string &dstPath,
                                     const AudioTargetFormat &target) {
        return ConvertWithFallback(srcPath, dstPath, target);
    }

    // AudioFileConverter
    bool AudioFileConverter::Convert(const std::string &srcPath, const std::string &dstPath,
                                     const AudioTargetFormat &target) {
        WavFormatConverter wavConverter;
        Mp3FormatConverter mp3Converter;
        if (wavConverter.CanHandle(srcPath)) {
            return wavConverter.Convert(srcPath, dstPath, target);
        }
        if (mp3Converter.CanHandle(srcPath)) {
            return mp3Converter.Convert(srcPath, dstPath, target);
        }
        SLOG_ERROR << "AudioFileConverter::Convert: unsupported format, src=" << srcPath;
        return false;
    }

    bool AudioFileConverter::IsSupportedAudioFile(const std::string &srcPath) {
        return IsWavFile(srcPath) || IsMp3File(srcPath);
    }

    bool AudioFileConverter::IsWavFile(const std::string &srcPath) {
        return GetLowerExtension(srcPath) == ".wav";
    }

    bool AudioFileConverter::IsMp3File(const std::string &srcPath) {
        return GetLowerExtension(srcPath) == ".mp3";
    }

    // 目前只有文件上传时调用，因为需要调用 ffprobe 获取时长效率不高，所以就入口校验
    // -1: 未找到 ffprobe,非关键失败
    // -2: 读取文件失败
    // 其他: 时长
    int64_t AudioFileConverter::GetAudioDurationSeconds(const std::string &filePath) {
        std::string ffprobeBin = FindFFprobeBinary();
        if (ffprobeBin.empty()) {
            SLOG_ERROR << "GetAudioDurationSeconds: ffprobe not found";
            return -1;
        }
        // ffprobe 输出秒数
        // 注意: 清空 LD_LIBRARY_PATH, 避免父进程的 sophon 旧版 libavformat(58.20.100)
        // 覆盖系统库(58.76.100), 否则会触发 "undefined symbol: avio_protocol_get_class" 错误
        std::string cmd = "env LD_LIBRARY_PATH= " + ShellQuote(ffprobeBin) +
                          " -v error -show_entries format=duration -of "
                          "default=noprint_wrappers=1:nokey=1 " +
                          ShellQuote(filePath) + " 2>&1";
        FILE* pipe = ::popen(cmd.c_str(), "r");
        if (pipe != nullptr) {
            std::array<char, 64> buf {};
            std::string output;
            while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
                output += buf.data();
            }
            ::pclose(pipe);
            // 去除首尾空白
            while (!output.empty() && (output.back() == '\n' || output.back() == '\r' || output.back() == ' ')) {
                output.pop_back();
            }
            if (!output.empty() && output != "N/A") {
                try {
                    double dur = std::stod(output);
                    if (dur > 0) {
                        return static_cast<int64_t>(dur);
                    }
                } catch (...) {
                    SLOG_WARN << "GetAudioDurationSeconds: parse ffprobe output failed, output=" << output;
                }
            }
        }
        return -2;
    }

}  // namespace qifeng_ca
