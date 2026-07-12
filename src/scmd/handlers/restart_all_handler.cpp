/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/restart_all_handler.h"

#include "common/types.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/handler_registry.h"
#include "service_manger/key_recoder.h"
#include "service_manger/service_context.h"
#include "service_manger/service_manager.h"

namespace qifeng::scm {

    ScmCommand RestartAllHandler::GetCommand() const {
        return ScmCommand::RESTART_ALL;
    }

    ScmResponse RestartAllHandler::Handle(const ScmRequest& /*request*/,
                                          const ServiceContext& ctx,
                                          KeyOperationRecorder& /*recorder*/) {
        ScmResponse response;
        SLOG_INFO << "Restarting all services";

        // 先停止所有服务
        auto stopResult = ctx.serviceManager->StopAllServices();
        if (!stopResult.IsDefaultSuccess()) {
            SLOG_WARN << "Some services failed to stop: " << stopResult.msg;
        }

        // 再启动所有autoStart服务
        auto startResult = ctx.serviceManager->StartAllAutoStartServices();
        if (!startResult.IsDefaultSuccess()) {
            SLOG_WARN << "Some services failed to start: " << startResult.msg;
        }

        // 任一步骤失败则返回警告
        ResultMsg result;
        if (!stopResult.IsDefaultSuccess() || !startResult.IsDefaultSuccess()) {
            result = MakeWarning("Some services failed during restart");
        } else {
            SLOG_INFO << "All services restarted successfully";
            result = MakeSuccess();
        }

        response.code = result.code;
        response.message = result.msg;
        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::RESTART_ALL, RestartAllHandler)

}  // namespace qifeng::scm
