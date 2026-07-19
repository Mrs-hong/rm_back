/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "common/types.h"

#include <string>

namespace qifeng::scm::utils {
    /**
     * @brief 写入升级结果 JSON 文件
     * @details 写入格式对齐 upgrade_result.json 模板：
     *          { "upgrade_success": <bool>, "upgrade_time": "<time>",
     *            "upgrade_version": "<version>", "defeat_reason": "<reason>" }
     *          会自动创建父目录，覆盖写。
     * @param resultPath 结果文件绝对路径
     * @param success 升级是否成功
     * @param upgradeTime 升级时间字符串（格式 YYYY-MM-DD HH:MM:SS）
     * @param upgradeVersion 升级到的目标版本号（失败场景若无法解析则为空）
     * @param defeatReason 失败原因（成功时可为空）
     * @return ResultMsg 写入结果
     */
    ResultMsg WriteUpgradeResult(const std::string &resultPath, bool success, const std::string &upgradeTime,
                                const std::string &upgradeVersion, const std::string &defeatReason);

}  // namespace qifeng::scm::utils
