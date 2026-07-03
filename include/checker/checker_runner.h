/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <json/json.h>
#include <string>

namespace qifeng::scm {

    /**
     * @brief 设备自检运行器
     * @details 封装 checker 库的调用，提供统一的运行接口和结果转换。
     *          被 ScmServer（开机自检）和 CheckHandler（手动触发）共同使用。
     */
    class CheckerRunner {
    public:
        /**
         * @brief 自检报告
         */
        struct CheckReport {
            bool overallOk{true};           // 整体是否通过（critical 项无 FAIL）
            std::string overallStatus;      // "OK" 或 "FAIL"
            std::string reportPath;         // JSON 报告文件路径
            Json::Value details;            // 各检查项详情
            std::string summary;            // 摘要文本（用于日志和响应）
        };

        /**
         * @brief 执行设备自检
         * @param configPath selftest.json 配置文件路径
         * @return CheckReport 自检报告
         */
        static CheckReport Run(const std::string& configPath);
    };

}  // namespace qifeng::scm
