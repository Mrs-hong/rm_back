/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "common/types.h"

#include <string>

namespace qifeng::scm::utils {
    /**
     * @description: 创建符号软连接
     * @param {string} &target 目标路径
     * @param {string} &linkPath 链接路径
     * @param {string} &linkPath 链接路径
     * @return {ResultMsg} 创建结果
     */
    ResultMsg CreateSymbolicLink(const std::string &target, const std::string &linkPath);

    /**
     * @description: 删除符号软连接
     * @param {string} &linkPath 链接路径
     * @return {ResultMsg} 删除结果
     */
    ResultMsg DeleteSymbolicLink(const std::string &linkPath);

}  // namespace qifeng::scm::utils
