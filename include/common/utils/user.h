/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "common/scmd_def.h"
#include "common/types.h"

#include <string>

namespace qifeng::scm::utils {
    /**
     * @brief 检查用户是否为 root 用户
     * @return bool 是否为 root 用户
     */
    bool IsRootUser();

    /**
     * @brief 检查用户是否存在
     * @param user 用户名
     * @return bool 是否存在
     */
    bool ExistUser(const std::string &user);

    /**
     * @brief 获取当前用户
     * @return std::string 当前用户
     */
    std::string GetCurrentUserName();

    /**
     * @brief 设置文件（目录或者文件）权限，完全归属指定用户和 root 组
     * @param path 文件或目录路径
     * @param user 目标用户名
     * @param mode 文件权限模式（如 0750），默认为 DefaultMode
     * @return ResultMsg 操作结果
     */
    ResultMsg SetFilePermission(const std::string &path, const std::string &user, int mode = DefaultMode);

}  // namespace qifeng::scm::utils
