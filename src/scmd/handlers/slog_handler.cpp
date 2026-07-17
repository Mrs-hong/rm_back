/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/slog_handler.h"

#include "common/types.h"
#include "ipc/data_def.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/service_ctl.h"
#include "service_manger/key_recoder.h"

namespace qifeng::scm {

    ScmCommand SlogHandler::GetCommand() const {
        return ScmCommand::SLOG;
    }

    ScmResponse SlogHandler::Handle(const ScmRequest& request,
                                    ServiceControl& serviceControl,
                                    KeyOperationRecorder& /*recorder*/) {
        const auto* params = std::get_if<SlogRequest>(&request.data);
        if (params == nullptr || params->serviceName.empty()) {
            SLOG_WARN << "Slog command missing service name";
            ScmResponse response;
            response.code = -1;
            response.message = "Slog command requires service name";
            return response;
        }

        ScmResponse response;
        auto result = serviceControl.GetServiceJournal(params->serviceName, params->logCount);
        response.code = result.code;
        response.message = result.msg;
        return response;
    }

}  // namespace qifeng::scm
