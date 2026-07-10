/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <string>

namespace qifeng::scm::utils {
    /**
     * @brief 判断字符串是否以指定后缀结尾
     * @details 替代 std::string::find_last_of 用于子串匹配
     *          （find_last_of 接受字符集合而非子串，行为不一致）
     * @param str 待判断的字符串
     * @param suffix 后缀子串
     * @return bool true 表示 str 以 suffix 结尾
     *
     * @example
     *   HasSuffix("file.tar.gz", ".tar.gz")  → true
     *   HasSuffix("file.tgz", ".tgz")        → true
     *   HasSuffix("file.txt", ".tar.gz")     → false
     */
    bool HasSuffix(const std::string &str, const std::string &suffix);
}  // namespace qifeng::scm::utils
