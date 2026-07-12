/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/init_nginx_handler.h"

#include "common/types.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/handler_registry.h"
#include "service_manger/key_recoder.h"
#include "service_manger/nginx_manager.h"
#include "service_manger/service_context.h"

namespace qifeng::scm {

    static std::optional<InitNginxRequest> FromJson(const Json::Value& params) {
        InitNginxRequest req;
        if (params.isMember("dirPath") && params["dirPath"].isString()) {
            req.dirPath = params["dirPath"].asString();
        }
        return req;
    }

    ScmCommand InitNginxHandler::GetCommand() const {
        return ScmCommand::INIT_NGINX;
    }

    ScmResponse InitNginxHandler::Handle(const ScmRequest& request,
                                         const ServiceContext& ctx,
                                         KeyOperationRecorder& /*recorder*/) {
        auto paramsOpt = FromJson(request.params);
        if (!paramsOpt.has_value() || paramsOpt->dirPath.empty()) {
            SLOG_WARN << "init_nginx command missing dir path";
            ScmResponse response;
            response.code = -1;
            response.message = "init_nginx requires --dir";
            return response;
        }

        ScmResponse response;
        auto result = ctx.nginxManager->InitNginx(paramsOpt->dirPath);
        response.code = result.code;
        response.message = result.msg;
        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::INIT_NGINX, InitNginxHandler)

}  // namespace qifeng::scm
