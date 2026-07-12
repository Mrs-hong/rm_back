/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/check_handler.h"

#include "checker/checker_runner.h"
#include "common/json_load.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/handler_registry.h"
#include "service_manger/key_recoder.h"
#include "service_manger/service_context.h"

namespace qifeng::scm {

    static std::optional<CheckRequest> FromJson(const Json::Value& params) {
        CheckRequest req;
        if (params.isObject() && params.isMember("configPath") && params["configPath"].isString()) {
            req.configPath = params["configPath"].asString();
        }
        return req;
    }

    ScmCommand CheckHandler::GetCommand() const {
        return ScmCommand::CHECK;
    }

    ScmResponse CheckHandler::Handle(const ScmRequest& request,
                                      const ServiceContext& /*ctx*/,
                                      KeyOperationRecorder& /*recorder*/) {
        // 支持请求中指定配置路径，为空则使用构造时传入的默认路径
        auto params = FromJson(request.params);
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
