//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_UTILS_FILE_NAME_GENERATOR_H
#define QIFENG_CA_INCLUDE_COMMON_UTILS_FILE_NAME_GENERATOR_H

#include <array>
#include <chrono>
#include <ctime>
#include <string>
#include <string_view>

#include "common/config/meeting_config.h"
#include "common/config/voiceprint_config.h"

namespace qifeng_ca {

    class FileNameGenerator {
    public:
        // 生成录音文件名: audio_{YYYY_MM_DD_HH_MM_SS}{suffix}
        static std::string GenAudioFile(std::string_view suffix = "") { return GenFile("audio", suffix); }

        // 生成声纹文件名: voice_{YYYY_MM_DD_HH_MM_SS}{suffix}
        static std::string GenVoiceFile(std::string_view suffix = "") { return GenFile("voice", suffix); }

        // 通用文件名生成: {prefix}_{YYYY_MM_DD_HH_MM_SS}{suffix}
        static std::string GenFile(std::string_view prefix, std::string_view suffix = "") {
            auto now = std::chrono::system_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
            auto sec = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
            // auto fracMs = static_cast<int>(ms.count() % 1000);

            std::time_t timeT = static_cast<std::time_t>(sec.count());
            struct tm tmBuf {};
            localtime_r(&timeT, &tmBuf);

            // 格式: prefix_YYYY_MM_DD_HH_MM_SS
            auto tsStr = FormatTimestamp(tmBuf);

            std::string result;
            result.reserve(prefix.size() + 1 + tsStr.size() + suffix.size());
            result.append(prefix);
            result.push_back('_');
            result.append(tsStr);
            result.append(suffix);
            return result;
        }

    private:
        // 将tm+毫秒格式化为 "YYYY_MM_DD_HH_MM_SS"
        static std::string FormatTimestamp(const struct tm &t) {
            std::string result;
            result.reserve(20);
            PadInt(result, t.tm_year + 1900, 4);
            result.push_back('_');
            PadInt(result, t.tm_mon + 1, 2);
            result.push_back('_');
            PadInt(result, t.tm_mday, 2);
            result.push_back('_');
            PadInt(result, t.tm_hour, 2);
            result.push_back('_');
            PadInt(result, t.tm_min, 2);
            result.push_back('_');
            PadInt(result, t.tm_sec, 2);
            // result.push_back('_');
            // PadInt(result, fracMs, 3);
            return result;
        }

        // 将整数按指定宽度补零追加到字符串
        static void PadInt(std::string &out, int val, int width) {
            std::array<char, 8> buf {};
            int pos = width - 1;
            for (int i = pos; i >= 0; --i) {
                buf[static_cast<size_t>(i)] = static_cast<char>('0' + val % 10);
                val /= 10;
            }
            out.append(buf.data(), static_cast<size_t>(width));
        }
    };

    // 根据audioId生成相对文件名(如: {audioId}.wav)
    inline std::string GetAudioFileName(const std::string &audioId) {
        return audioId + ".wav";
    }

    // 根据accountId和audioId生成绝对路径(如: data/audio/{accountId}/{audioId}.wav)
    inline std::string GetAudioFilePath(uint64_t accountId, const std::string &audioId) {
        return MeetingConfig::GetInstance().GetAudioPath() + "/" + std::to_string(accountId) + "/" +
               GetAudioFileName(audioId);
    }

    // 根据accountId和audioId生成软连接目录下的路径(如: data/src/audio/{accountId}/{audioId}.wav)
    inline std::string GetSymlinkAudioFilePath(uint64_t accountId, const std::string &audioId) {
        return MeetingConfig::GetInstance().GetSymlinkAudioPath() + "/" + std::to_string(accountId) + "/" +
               GetAudioFileName(audioId);
    }

    // 根据accountId生成绝对路径(如: /data/voiceprint/{accountId}/{accountId}.wav)
    inline std::string GetVoiceprintFilePath(uint64_t accountId) {
        return VoiceprintConfig::GetInstance().GetVoiceprintTmpPath() + std::to_string(accountId) + "/";
    }

    // 根据accountId和audioId生成相对文件名(如: {accountId}.wav)
    inline std::string GetVoiceprintFileName(uint64_t accountId, const std::string &audioId) {
        return GetVoiceprintFilePath(accountId) + audioId + ".wav";
    }

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_UTILS_FILE_NAME_GENERATOR_H
