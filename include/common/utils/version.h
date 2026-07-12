/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <string>

namespace qifeng::scm::utils {
    /**
     * @brief 比较两个版本号
     * @param lhs 左操作数版本号，格式 x.x.x
     * @param rhs 右操作数版本号，格式 x.x.x
     * @return int lhs < rhs 返回 -1，lhs == rhs 返回 0，lhs > rhs 返回 1
     */
    int CompareVersion(const std::string &lhs, const std::string &rhs);

    /**
     * @brief 检查实际版本是否满足约束条件
     * @param actualVersion 实际版本号，格式 x.x.x
     * @param constraint 版本约束，支持 >=x.x.x、>x.x.x、<=x.x.x、<x.x.x、=x.x.x、x.x.x
     * @return bool 是否满足约束
     */
    bool SatisfiesVersionConstraint(const std::string &actualVersion, const std::string &constraint);

}  // namespace qifeng::scm::utils
