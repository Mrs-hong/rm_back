/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/reset_nginx_handler.h"

#include "common/types.h"
#include "ipc/data_def.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/service_ctl.h"
#include "service_manger/key_recoder.h"

namespace qifeng::scm {

    ScmCommand ResetNginxHandler::GetCommand() const {
        return ScmCommand::RESET_NGINX;
    }

    ScmResponse ResetNginxHandler::Handle(const ScmRequest& request,
                                           ServiceControl& serviceControl,
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
        auto result = serviceControl.ResetNginx(params->mode);
        response.code = result.code;
        response.message = result.msg;
        return response;
    }

}  // namespace qifeng::scm
