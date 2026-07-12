/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/reload_handler.h"

#include "common/types.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/handler_registry.h"
#include "service_manger/key_recoder.h"
#include "service_manger/service_context.h"
#include "service_manger/service_manager.h"

namespace qifeng::scm {

    static std::optional<ReloadRequest> FromJson(const Json::Value& params) {
        ReloadRequest req;
        if (params.isMember("serviceName") && params["serviceName"].isString()) {
            req.serviceName = params["serviceName"].asString();
        }
        return req;
    }

    ScmCommand ReloadHandler::GetCommand() const {
        return ScmCommand::RELOAD;
    }

    ScmResponse ReloadHandler::Handle(const ScmRequest& request,
                                      const ServiceContext& ctx,
                                      KeyOperationRecorder& /*recorder*/) {
        auto paramsOpt = FromJson(request.params);
        if (!paramsOpt || paramsOpt->serviceName.empty()) {
            SLOG_WARN << "Reload command missing service name";
            ScmResponse response;
            response.code = -1;
            response.message = "Reload command requires service name";
            return response;
        }

        const auto& params = *paramsOpt;
        ScmResponse response;
        auto result = ctx.serviceManager->ReloadService(params.serviceName);
        response.code = result.code;
        response.message = result.msg;
        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::RELOAD, ReloadHandler)

}  // namespace qifeng::scm
