/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/kill_handler.h"

#include "ipc/data_def.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/handler_registry.h"
#include "service_manger/key_recoder.h"
#include "service_manger/service_context.h"

namespace qifeng::scm {

    ScmCommand KillHandler::GetCommand() const {
        return ScmCommand::KILL;
    }

    ScmResponse KillHandler::Handle(const ScmRequest& /*request*/,
                                    const ServiceContext& /*ctx*/,
                                    KeyOperationRecorder& /*recorder*/) {
        SLOG_INFO << "Received KILL command, initiating graceful shutdown";
        ScmResponse response;
        response.code = 0;
        response.message = "scmd is shutting down gracefully";
        // 延迟停止，确保响应先发送回客户端
        if (mShutdownCallback) {
            mShutdownCallback();
        }
        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::KILL, KillHandler)

}  // namespace qifeng::scm
