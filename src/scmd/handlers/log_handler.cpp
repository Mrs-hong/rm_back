/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/log_handler.h"

#include "common/types.h"
#include "ipc/data_def.h"
#include "scmd/handler_registry.h"
#include "scmd/service_ctl.h"
#include "service_manger/key_recoder.h"

namespace qifeng::scm {

    ScmCommand LogHandler::GetCommand() const {
        return ScmCommand::LOG;
    }

    ScmResponse LogHandler::Handle(const ScmRequest& request,
                                   ServiceControl& serviceControl,
                                   KeyOperationRecorder& /*recorder*/) {
        const auto* params = std::get_if<LogRequest>(&request.data);
        if (params == nullptr) {
            ScmResponse response;
            response.code = -1;
            response.message = "Invalid log request";
            return response;
        }

        ScmResponse response;
        auto result = serviceControl.GetOperationLog(params->logLevel, params->logCount);
        response.code = result.code;
        response.message = result.msg;
        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::LOG, LogHandler)

}  // namespace qifeng::scm
