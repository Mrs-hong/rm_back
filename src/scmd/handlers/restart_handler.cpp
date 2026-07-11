/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/restart_handler.h"

#include "common/types.h"
#include "ipc/data_def.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/handler_registry.h"
#include "scmd/service_ctl.h"
#include "service_manger/key_recoder.h"

namespace qifeng::scm {

    ScmCommand RestartHandler::GetCommand() const {
        return ScmCommand::RESTART;
    }

    ScmResponse RestartHandler::Handle(const ScmRequest& request,
                                       ServiceControl& serviceControl,
                                       KeyOperationRecorder& /*recorder*/) {
        const auto* params = std::get_if<RestartRequest>(&request.data);
        if (params == nullptr || params->serviceName.empty()) {
            // 未指定服务名时，操作 scmd 自身
            SLOG_INFO << "Restart command without service name, operating on scmd self";
            ScmResponse response;
            auto result = serviceControl.RestartScmdSelf();
            response.code = result.code;
            response.message = result.msg;
            return response;
        }

        ScmResponse response;
        auto result = serviceControl.RestartService(params->serviceName);
        response.code = result.code;
        response.message = result.msg;
        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::RESTART, RestartHandler)

}  // namespace qifeng::scm
