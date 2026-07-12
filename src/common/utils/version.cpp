/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/utils/version.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace qifeng::scm::utils {
    /**
     * @brief 将版本号字符串拆分为数字段
     * @param version 版本号字符串，如 "1.2.3"
     * @return std::vector<int> 各段数字
     */
    static std::vector<int> ParseVersionParts(const std::string &version) {
        std::vector<int> parts;
        std::stringstream ss(version);
        std::string token;
        while (std::getline(ss, token, '.')) {
            try {
                parts.push_back(std::stoi(token));
            } catch (...) {
                parts.push_back(0);
            }
        }
        return parts;
    }

    int CompareVersion(const std::string &lhs, const std::string &rhs) {
        auto leftParts = ParseVersionParts(lhs);
        auto rightParts = ParseVersionParts(rhs);
        size_t maxLen = std::max(leftParts.size(), rightParts.size());
        for (size_t i = 0; i < maxLen; ++i) {
            int l = (i < leftParts.size()) ? leftParts[i] : 0;
            int r = (i < rightParts.size()) ? rightParts[i] : 0;
            if (l < r) {
                return -1;
            }
            if (l > r) {
                return 1;
            }
        }
        return 0;
    }

    bool SatisfiesVersionConstraint(const std::string &actualVersion, const std::string &constraint) {
        if (actualVersion.empty() || constraint.empty()) {
            return false;
        }

        // 解析约束条件中的操作符和版本号
        std::string op;
        std::string expectedVersion;
        if (constraint.rfind(">=", 0) == 0) {
            op = ">=";
            expectedVersion = constraint.substr(2);
        } else if (constraint.rfind("<=", 0) == 0) {
            op = "<=";
            expectedVersion = constraint.substr(2);
        } else if (constraint.rfind(">", 0) == 0) {
            op = ">";
            expectedVersion = constraint.substr(1);
        } else if (constraint.rfind("<", 0) == 0) {
            op = "<";
            expectedVersion = constraint.substr(1);
        } else if (constraint.rfind("=", 0) == 0) {
            op = "=";
            expectedVersion = constraint.substr(1);
        } else {
            // 无操作符前缀，默认精确匹配
            op = "=";
            expectedVersion = constraint;
        }

        int cmp = CompareVersion(actualVersion, expectedVersion);
        if (op == ">=") {
            return cmp >= 0;
        }
        if (op == ">") {
            return cmp > 0;
        }
        if (op == "<=") {
            return cmp <= 0;
        }
        if (op == "<") {
            return cmp < 0;
        }
        // 精确匹配
        return cmp == 0;
    }
}  // namespace qifeng::scm::utils
