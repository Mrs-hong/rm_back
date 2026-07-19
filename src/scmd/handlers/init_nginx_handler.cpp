/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/init_nginx_handler.h"
#include "scmd/handler_registry.h"

#include "common/types.h"
#include "ipc/data_def.h"
#include "qifeng_framework/common/logger.h"
#include "service_manager/key_recoder.h"
#include "service_manager/nginx_manager.h"
#include "service_manager/service_context.h"

namespace qifeng::scm {

    ScmCommand InitNginxHandler::GetCommand() const {
        return ScmCommand::INIT_NGINX;
    }

    ScmResponse InitNginxHandler::Handle(const ScmRequest& request,
                                          const ServiceContext& ctx,
                                          KeyOperationRecorder& /*recorder*/) {
        const auto* params = std::get_if<InitNginxRequest>(&request.data);
        if (params == nullptr || params->dirPath.empty()) {
            SLOG_WARN << "init_nginx command missing dir path";
            ScmResponse response;
            response.code = -1;
            response.message = "init_nginx requires --dir";
            return response;
        }

        ScmResponse response;
        auto result = ctx.nginxManager->InitNginx(params->dirPath);
        response.code = result.code;
        response.message = result.msg;
        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::INIT_NGINX, InitNginxHandler)

}  // namespace qifeng::scm
