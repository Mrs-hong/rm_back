/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/self_check_service.h"

#include "checker/checker_runner.h"
#include "common/config.h"
#include "qifeng_framework/common/logger.h"

#include <iostream>

namespace qifeng::scm {

    SelfCheckService::SelfCheckService(const ConfigLoader& configLoader) : mConfigLoader(configLoader) {
    }

    bool SelfCheckService::Run() {
        // 从配置中获取自检开关和路径
        const auto& configInfo = mConfigLoader.GetConfigInfo();

        // 检查是否启用开机自检
        if (!configInfo.selftestEnabled) {
            SLOG_INFO << "Startup self-check is disabled by configuration";
            std::cout << "[scmd] 开机自检已禁用" << std::endl;
            return true;
        }

        // 确定自检配置路径
        if (!configInfo.selftestConfigPath.empty()) {
            mSelfTestConfigPath = configInfo.selftestConfigPath;
        } else {
            mSelfTestConfigPath = configInfo.configDir + "/selftest.json";
        }

        SLOG_INFO << "Running startup self-check with config: " << mSelfTestConfigPath;

        // 加载 selftest.json 到 mJsonLoader（由 SelfCheckService 管理，供 CheckerRunner 使用）
        // 文件加载失败时 JsonLoad 内部保留空根对象，CheckerRunner 会使用各 checker 的默认配置
        if (mJsonLoader.LoadFromFile(mSelfTestConfigPath)) {
            SLOG_INFO << "selftest json loaded into mJsonLoader";
        } else {
            SLOG_WARN << "selftest json load failed, fallback to built-in defaults";
        }

        auto report = CheckerRunner::Run(mJsonLoader);

        // 输出自检结果摘要到控制台
        std::cout << "[scmd] 开机自检完成: " << report.overallStatus << " (" << report.summary << ")" << std::endl;

        if (!report.overallOk) {
            SLOG_WARN << "Startup self-check FAILED: " << report.summary;

            // 根据配置决定自检失败后的行为
            if (configInfo.selftestFailAction == "halt") {
                SLOG_ERROR << "Self-check fail action is 'halt', aborting startup";
                std::cout << "[scmd] 自检失败且 fail_action=halt，服务将停止" << std::endl;
                return false;
            }
            // 默认行为：仅告警，不阻止服务启动
        }

        return true;
    }

}  // namespace qifeng::scm
