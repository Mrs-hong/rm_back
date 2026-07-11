/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/check_handler.h"

#include "checker/checker_runner.h"
#include "common/json_load.h"
#include "ipc/data_def.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/handler_registry.h"
#include "scmd/service_ctl.h"
#include "service_manger/key_recoder.h"

namespace qifeng::scm {

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

        // 加载配置文件并执行自检；加载失败时 JsonLoad 内部保留空根对象，
        // CheckerRunner 会使用各 checker 的默认配置继续执行
        JsonLoad loader;
        loader.LoadFromFile(configPath);
        auto report = CheckerRunner::Run(loader);

        // 组装响应
        ScmResponse response;
        response.code = report.overallOk ? 0 : -1;
        response.message = "Self-test " + report.overallStatus + ": " + report.summary;
        response.data["overall"] = report.overallStatus;
        response.data["reportPath"] = report.reportPath;
        response.data["checks"] = report.details;

        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::CHECK, CheckHandler)

}  // namespace qifeng::scm
