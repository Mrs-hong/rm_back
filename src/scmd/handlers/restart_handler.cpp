/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/restart_handler.h"

#include "common/types.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/handler_registry.h"
#include "service_manger/key_recoder.h"
#include "service_manger/service_context.h"
#include "service_manger/service_manager.h"

namespace qifeng::scm {

    static std::optional<RestartRequest> FromJson(const Json::Value& params) {
        RestartRequest req;
        if (params.isMember("serviceName") && params["serviceName"].isString()) {
            req.serviceName = params["serviceName"].asString();
        }
        return req;
    }

    ScmCommand RestartHandler::GetCommand() const {
        return ScmCommand::RESTART;
    }

    ScmResponse RestartHandler::Handle(const ScmRequest& request,
                                       const ServiceContext& ctx,
                                       KeyOperationRecorder& /*recorder*/) {
        auto paramsOpt = FromJson(request.params);
        if (!paramsOpt || paramsOpt->serviceName.empty()) {
            // 未指定服务名时，操作 scmd 自身
            SLOG_INFO << "Restart command without service name, operating on scmd self";
            ScmResponse response;
            auto result = ctx.serviceManager->RestartScmdSelf();
            response.code = result.code;
            response.message = result.msg;
            return response;
        }

        const auto& params = *paramsOpt;
        ScmResponse response;
        auto result = ctx.serviceManager->RestartService(params.serviceName);
        response.code = result.code;
        response.message = result.msg;
        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::RESTART, RestartHandler)

}  // namespace qifeng::scm
