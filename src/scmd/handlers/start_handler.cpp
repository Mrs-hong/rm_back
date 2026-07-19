/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/start_handler.h"
#include "scmd/handler_registry.h"

#include "common/types.h"
#include "ipc/data_def.h"
#include "qifeng_framework/common/logger.h"
#include "service_manager/key_recoder.h"
#include "service_manager/service_context.h"
#include "service_manager/service_manager.h"

namespace qifeng::scm {

    ScmCommand StartHandler::GetCommand() const {
        return ScmCommand::START;
    }

    ScmResponse StartHandler::Handle(const ScmRequest& request,
                                     const ServiceContext& ctx,
                                     KeyOperationRecorder& recorder) {
        const auto* params = std::get_if<StartRequest>(&request.data);
        if (params == nullptr || params->serviceName.empty()) {
            // 未指定服务名时，操作 scmd 自身
            SLOG_INFO << "Start command without service name, operating on scmd self";
            auto result = ctx.serviceManager->StartScmdSelf();
            ScmResponse response;
            response.code = result.code;
            response.message = result.msg;
            return response;
        }

        ScmResponse response;
        recorder.RecordOperation({"start", params->serviceName, 2, "", ""});
        auto result = ctx.serviceManager->StartService(params->serviceName);
        response.code = result.code;
        response.message = result.msg;
        recorder.UpdateResult(result.IsDefaultSuccess() ? 0 : 1);
        if (result.IsDefaultSuccess()) {
            recorder.Clear();
        }
        return response;
    }

    ResultMsg StartHandler::Recover(const KeyOperationRecord& record, const ServiceContext& ctx) {
        SLOG_INFO << "Start was interrupted, retrying: " << record.serviceName;
        return ctx.serviceManager->StartService(record.serviceName);
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::START, StartHandler)

}  // namespace qifeng::scm
