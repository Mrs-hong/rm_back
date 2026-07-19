/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/reset_nginx_handler.h"
#include "scmd/handler_registry.h"

#include "common/types.h"
#include "ipc/data_def.h"
#include "qifeng_framework/common/logger.h"
#include "service_manager/key_recoder.h"
#include "service_manager/nginx_manager.h"
#include "service_manager/service_context.h"

namespace qifeng::scm {

    ScmCommand ResetNginxHandler::GetCommand() const {
        return ScmCommand::RESET_NGINX;
    }

    ScmResponse ResetNginxHandler::Handle(const ScmRequest& request,
                                           const ServiceContext& ctx,
                                           KeyOperationRecorder& /*recorder*/) {
        const auto* params = std::get_if<ResetNginxRequest>(&request.data);
        if (params == nullptr) {
            SLOG_WARN << "reset_nginx command invalid params";
            ScmResponse response;
            response.code = -1;
            response.message = "reset_nginx: invalid request";
            return response;
        }

        ScmResponse response;
        auto result = ctx.nginxManager->ResetNginx(params->mode);
        response.code = result.code;
        response.message = result.msg;
        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::RESET_NGINX, ResetNginxHandler)

}  // namespace qifeng::scm
