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
        if (params == nullptr) {
            SLOG_WARN << "Slog command missing request params";
            ScmResponse response;
            response.code = -1;
            response.message = "Slog command missing request params";
            return response;
        }

        // serviceName 为空时查 scmd 自身日志（qifeng-scm.log），由 GetServiceLog 处理
        ScmResponse response;
        auto result = serviceControl.GetServiceLog(params->serviceName, params->logCount);
        response.code = result.code;
        response.message = result.msg;
        return response;
    }

}  // namespace qifeng::scm
