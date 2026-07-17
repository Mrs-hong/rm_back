/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/init_nginx_handler.h"

#include "common/types.h"
#include "ipc/data_def.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/service_ctl.h"
#include "service_manger/key_recoder.h"

namespace qifeng::scm {

    ScmCommand InitNginxHandler::GetCommand() const {
        return ScmCommand::INIT_NGINX;
    }

    ScmResponse InitNginxHandler::Handle(const ScmRequest& request,
                                          ServiceControl& serviceControl,
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
        auto result = serviceControl.InitNginx(params->dirPath);
        response.code = result.code;
        response.message = result.msg;
        return response;
    }

}  // namespace qifeng::scm
