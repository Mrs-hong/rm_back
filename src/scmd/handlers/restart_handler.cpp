/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/restart_handler.h"
#include "scmd/handler_registry.h"

#include "common/types.h"
#include "ipc/data_def.h"
#include "qifeng_framework/common/logger.h"
#include "service_manager/key_recoder.h"
#include "service_manager/service_context.h"
#include "service_manager/service_manager.h"

namespace qifeng::scm {

    ScmCommand RestartHandler::GetCommand() const {
        return ScmCommand::RESTART;
    }

    ScmResponse RestartHandler::Handle(const ScmRequest& request,
                                       const ServiceContext& ctx,
                                       KeyOperationRecorder& /*recorder*/) {
        const auto* params = std::get_if<RestartRequest>(&request.data);
        if (params == nullptr || params->serviceName.empty()) {
            // 未指定服务名时，操作 scmd 自身
            SLOG_INFO << "Restart command without service name, operating on scmd self";
            ScmResponse response;
            auto result = ctx.serviceManager->RestartScmdSelf();
            response.code = result.code;
            response.message = result.msg;
            return response;
        }

        ScmResponse response;
        auto result = ctx.serviceManager->RestartService(params->serviceName);
        response.code = result.code;
        response.message = result.msg;
        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::RESTART, RestartHandler)

}  // namespace qifeng::scm
