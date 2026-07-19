/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <string>

namespace qifeng::scm::utils {
    /**
     * @brief 生成随机密码
     * @param length 密码长度
     * @return std::string 随机密码
     */
    std::string GenerateRandomPassword(int length = 16);

}  // namespace qifeng::scm::utils
