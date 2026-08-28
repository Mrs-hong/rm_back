/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 *
 * file_utils.h - 文件操作工具类
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_FILE_UTILS_H
#define QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_FILE_UTILS_H

#include <filesystem>

namespace common {

    namespace utils {

        /**
         * @brief 从文件路径读取全部文本内容
         * @param[in] path 文件路径
         * @return 全部文本内容（非二进制）
         */
        std::string LoadAllContentFromFile(const std::filesystem::path& path);
    }

}

#endif