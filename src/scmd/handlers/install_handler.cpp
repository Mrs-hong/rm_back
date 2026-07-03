/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/install_handler.h"

#include "common/types.h"
#include "ipc/data_def.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/service_ctl.h"
#include "service_manger/key_recoder.h"

namespace qifeng::scm {

    ScmCommand InstallHandler::GetCommand() const {
        return ScmCommand::INSTALL;
    }

    ScmResponse InstallHandler::Handle(const ScmRequest& request,
                                       ServiceControl& serviceControl,
                                       KeyOperationRecorder& recorder) {
        const auto* params = std::get_if<InstallRequest>(&request.data);
        if (params == nullptr || params->serviceName.empty()) {
            SLOG_WARN << "Install command missing service name";
            ScmResponse response;
            response.code = -1;
            response.message = "Install command requires service name";
            return response;
        }

        ScmResponse response;
        recorder.RecordOperation({"install", params->serviceName, 2, params->tarDir, ""});
        auto result = serviceControl.Installed(params->serviceName, params->tarDir);
        response.code = result.code;
        response.message = result.msg;
        recorder.UpdateResult(result.IsDefalutSuccess() ? 0 : 1);
        if (result.IsDefalutSuccess()) {
            recorder.Clear();
        }
        return response;
    }

}  // namespace qifeng::scm
