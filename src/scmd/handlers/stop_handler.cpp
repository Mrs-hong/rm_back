/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/stop_handler.h"

#include "common/types.h"
#include "ipc/data_def.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/handler_registry.h"
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
            // 未指定服务名时，操作 scmd 自身
            SLOG_INFO << "Stop command without service name, operating on scmd self";
            ScmResponse response;
            auto result = serviceControl.StopScmdSelf();
            response.code = result.code;
            response.message = result.msg;
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

    ResultMsg StopHandler::Recover(const KeyOperationRecord& record, ServiceControl& serviceControl) {
        SLOG_INFO << "Stop was interrupted, retrying: " << record.serviceName;
        return serviceControl.StopService(record.serviceName);
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::STOP, StopHandler)

}  // namespace qifeng::scm
