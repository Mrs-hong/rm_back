/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/stop_handler.h"

#include "common/types.h"
#include "ipc/data_def.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/service_ctl.h"
#include "service_manger/key_recoder.h"

namespace qifeng::scm {

    ScmCommand StopHandler::GetCommand() const {
        return ScmCommand::STOP;
    }

    ScmResponse StopHandler::Handle(const ScmRequest& request,
                                    ServiceControl& serviceControl,
                                    KeyOperationRecorder& recorder) {
        const auto* params = std::get_if<StopRequest>(&request.data);
        if (params == nullptr || params->serviceName.empty()) {
            SLOG_WARN << "Stop command missing service name";
            ScmResponse response;
            response.code = -1;
            response.message = "Stop command requires service name";
            return response;
        }

        ScmResponse response;
        recorder.RecordOperation({"stop", params->serviceName, 2, "", ""});
        auto result = serviceControl.StopService(params->serviceName);
        response.code = result.code;
        response.message = result.msg;
        recorder.UpdateResult(result.IsDefalutSuccess() ? 0 : 1);
        if (result.IsDefalutSuccess()) {
            recorder.Clear();
        }
        return response;
    }

}  // namespace qifeng::scm
