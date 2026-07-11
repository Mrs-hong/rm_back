/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/reload_handler.h"

#include "common/types.h"
#include "ipc/data_def.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/handler_registry.h"
#include "scmd/service_ctl.h"
#include "service_manger/key_recoder.h"

namespace qifeng::scm {

    ScmCommand ReloadHandler::GetCommand() const {
        return ScmCommand::RELOAD;
    }

    ScmResponse ReloadHandler::Handle(const ScmRequest& request,
                                      ServiceControl& serviceControl,
                                      KeyOperationRecorder& /*recorder*/) {
        const auto* params = std::get_if<ReloadRequest>(&request.data);
        if (params == nullptr || params->serviceName.empty()) {
            SLOG_WARN << "Reload command missing service name";
            ScmResponse response;
            response.code = -1;
            response.message = "Reload command requires service name";
            return response;
        }

        ScmResponse response;
        auto result = serviceControl.ReloadService(params->serviceName);
        response.code = result.code;
        response.message = result.msg;
        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::RELOAD, ReloadHandler)

}  // namespace qifeng::scm
