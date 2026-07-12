/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/reset_nginx_handler.h"

#include "common/types.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/handler_registry.h"
#include "service_manger/key_recoder.h"
#include "service_manger/nginx_manager.h"
#include "service_manger/service_context.h"

namespace qifeng::scm {

    static std::optional<ResetNginxRequest> FromJson(const Json::Value& params) {
        ResetNginxRequest req;
        if (params.isMember("mode") && params["mode"].isInt()) {
            req.mode = static_cast<NginxResetMode>(params["mode"].asInt());
        }
        return req;
    }

    ScmCommand ResetNginxHandler::GetCommand() const {
        return ScmCommand::RESET_NGINX;
    }

    ScmResponse ResetNginxHandler::Handle(const ScmRequest& request,
                                          const ServiceContext& ctx,
                                          KeyOperationRecorder& /*recorder*/) {
        auto paramsOpt = FromJson(request.params);
        if (!paramsOpt.has_value()) {
            SLOG_WARN << "reset_nginx command invalid params";
            ScmResponse response;
            response.code = -1;
            response.message = "reset_nginx: invalid request";
            return response;
        }

        ScmResponse response;
        auto result = ctx.nginxManager->ResetNginx(paramsOpt->mode);
        response.code = result.code;
        response.message = result.msg;
        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::RESET_NGINX, ResetNginxHandler)

}  // namespace qifeng::scm
