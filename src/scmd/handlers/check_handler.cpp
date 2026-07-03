/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/check_handler.h"

#include "checker/checker_runner.h"
#include "ipc/data_def.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/service_ctl.h"
#include "service_manger/key_recoder.h"

namespace qifeng::scm {

    CheckHandler::CheckHandler(std::string configPath) : mConfigPath(std::move(configPath)) {
    }

    ScmCommand CheckHandler::GetCommand() const {
        return ScmCommand::CHECK;
    }

    ScmResponse CheckHandler::Handle(const ScmRequest& request,
                                      ServiceControl& /*serviceControl*/,
                                      KeyOperationRecorder& /*recorder*/) {
        // 支持请求中指定配置路径，为空则使用构造时传入的默认路径
        const auto* params = std::get_if<CheckRequest>(&request.data);
        std::string configPath = mConfigPath;
        if (params && !params->configPath.empty()) {
            configPath = params->configPath;
        }

        SLOG_INFO << "Running self-check with config: " << configPath;

        // 执行自检
        auto report = CheckerRunner::Run(configPath);

        // 组装响应
        ScmResponse response;
        response.code = report.overallOk ? 0 : -1;
        response.message = "Self-test " + report.overallStatus + ": " + report.summary;
        response.data["overall"] = report.overallStatus;
        response.data["reportPath"] = report.reportPath;
        response.data["checks"] = report.details;

        return response;
    }

}  // namespace qifeng::scm
