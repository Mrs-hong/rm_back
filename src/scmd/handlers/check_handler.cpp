/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/check_handler.h"

#include "checker/checker_runner.h"
#include "common/json_load.h"
#include "ipc/data_def.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/handler_registry.h"
#include "service_manager/key_recoder.h"
#include "service_manager/service_context.h"

namespace qifeng::scm {

    ScmCommand CheckHandler::GetCommand() const {
        return ScmCommand::CHECK;
    }

    ScmResponse CheckHandler::Handle(const ScmRequest& request,
                                      const ServiceContext& /*ctx*/,
                                      KeyOperationRecorder& /*recorder*/) {
        // 支持请求中指定配置路径，为空则使用构造时传入的默认路径
        const auto* params = std::get_if<CheckRequest>(&request.data);
        std::string configPath = mConfigPath;
        bool userSpecified = false;
        if (params && !params->configPath.empty()) {
            configPath = params->configPath;
            userSpecified = true;
        }

        SLOG_INFO << "Running self-check with config: " << configPath;

        // 加载配置文件
        JsonLoad loader;
        bool loaded = loader.LoadFromFile(configPath);

        // 用户显式指定配置路径时，加载失败视为错误（文件不存在/无读权限/JSON 解析失败）
        // 默认路径加载失败时保留原行为：使用空根继续，各 checker 用内置默认配置
        if (userSpecified && !loaded) {
            ScmResponse response;
            response.code = -1;
            response.message = "Failed to load selftest config: " + configPath;
            response.data["overall"] = "FAIL";
            return response;
        }

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
