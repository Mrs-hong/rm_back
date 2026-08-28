//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_MEETING_MEETING_CHECK_H
#define QIFENG_CA_INCLUDE_CORE_MEETING_MEETING_CHECK_H

#include <cstdint>
#include <filesystem>
#include <string>

#include "qifeng_framework/common/logger.h"
#include "utf8/checked.h"

#include "common/audio/audio_file_converter.h"

namespace qifeng_ca {
    // 准备单个离线音频文件: 通过 AudioFileConverter 转换为目标WAV格式
    // 支持 WAV(格式匹配则rename, 不匹配则重采样) 和 MP3(解码转WAV)
    inline std::string PrepareSingleAudioFile(const std::string &srcPath, const std::string &dstPath) {
        AudioTargetFormat target;
        if (!AudioFileConverter::Convert(srcPath, dstPath, target)) {
            SLOG_ERROR << "PrepareSingleAudioFile: convert failed, src=" << srcPath;
            return "";
        }
        // 转换成功后删除源文件(MP3转换或WAV重采样场景下源文件仍存在)
        if (srcPath != dstPath && std::filesystem::exists(srcPath)) {
            std::error_code ec;
            std::filesystem::remove(srcPath, ec);
        }
        SLOG_INFO << "PrepareSingleAudioFile: success, src=" << srcPath << " dst=" << dstPath;
        return dstPath;
    }

    // 主题字符校验: 仅允许中文、英文、数字和 _ - .
    // 使用 utf8::next 逐字符遍历 code point, 避免正则对 UTF-8 中文匹配的兼容性问题
    inline bool IsThemeCharValid(uint32_t cp) {
        if (cp >= 0x4E00 && cp <= 0x9FFF) {  // CJK 统一汉字
            return true;
        }
        if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z')) {
            return true;
        }
        if (cp >= '0' && cp <= '9') {
            return true;
        }
        return cp == '_' || cp == '-' || cp == '.';
    }

    inline bool IsThemeContentValid(const std::string &theme) {
        std::string::const_iterator it = theme.begin();
        std::string::const_iterator end = theme.end();
        try {
            while (it != end) {
                uint32_t cp = utf8::next(it, end);
                if (!IsThemeCharValid(cp)) {
                    return false;
                }
            }
        } catch (const std::exception &e) {
            SLOG_WARN << "IsThemeContentValid: invalid utf8 sequence, theme=" << theme << ", err=" << e.what();
            return false;
        }
        return true;
    }

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_MEETING_MEETING_CHECK_H
