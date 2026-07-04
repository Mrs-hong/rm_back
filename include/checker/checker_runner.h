/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <json/json.h>

#include <string>

#include "common/json_load.h"

namespace qifeng::scm {

    /**
     * @brief 设备自检运行器（对外统一入口）
     * @details 封装 CheckerRegistry 注册 + BuildAll + Runner 调度 + 结果汇总 + JSON 报告生成。
     *          输入为已加载的 JsonLoad（由 ScmServer 管理），避免重复读盘。
     *          被 ScmServer（开机自检）和 CheckHandler（手动触发）共同使用。
     */
    class CheckerRunner {
    public:
        /**
         * @brief 自检报告
         */
        struct CheckReport {
            bool overallOk{true};            // 整体是否通过（critical 项无 FAIL）
            std::string overallStatus;       // "OK" 或 "FAIL"
            std::string reportPath;          // JSON 报告文件路径
            Json::Value details;             // 各检查项详情
            std::string summary;             // 摘要文本（用于日志和响应）
        };

        /**
         * @brief 执行设备自检
         * @param loader 已加载 selftest.json 的 JsonLoad 对象
         * @return CheckReport 自检报告
         */
        static CheckReport Run(const JsonLoad &loader);
    };

}  // namespace qifeng::scm
