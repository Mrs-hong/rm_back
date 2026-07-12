/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/stop_handler.h"

#include "common/types.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/handler_registry.h"
#include "service_manger/key_recoder.h"
#include "service_manger/service_context.h"
#include "service_manger/service_manager.h"

namespace qifeng::scm {

    static std::optional<StopRequest> FromJson(const Json::Value& params) {
        StopRequest req;
        if (params.isMember("serviceName") && params["serviceName"].isString()) {
            req.serviceName = params["serviceName"].asString();
        }
        return req;
    }

    ScmCommand StopHandler::GetCommand() const {
        return ScmCommand::STOP;
    }

    ScmResponse StopHandler::Handle(const ScmRequest& request,
                                    const ServiceContext& ctx,
                                    KeyOperationRecorder& recorder) {
        auto paramsOpt = FromJson(request.params);
        if (!paramsOpt || paramsOpt->serviceName.empty()) {
            // 未指定服务名时，操作 scmd 自身
            SLOG_INFO << "Stop command without service name, operating on scmd self";
            ScmResponse response;
            auto result = ctx.serviceManager->StopScmdSelf();
            response.code = result.code;
            response.message = result.msg;
            return response;
        }

        const auto& params = *paramsOpt;
        ScmResponse response;
        recorder.RecordOperation({"stop", params.serviceName, 2, ""});
        auto result = ctx.serviceManager->StopService(params.serviceName);
        response.code = result.code;
        response.message = result.msg;
        recorder.UpdateResult(result.IsDefaultSuccess() ? 0 : 1);
        if (result.IsDefaultSuccess()) {
            recorder.Clear();
        }
        return response;
    }

    ResultMsg StopHandler::Recover(const KeyOperationRecord& record, const ServiceContext& ctx) {
        SLOG_INFO << "Stop was interrupted, retrying: " << record.serviceName;
        return ctx.serviceManager->StopService(record.serviceName);
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::STOP, StopHandler)

}  // namespace qifeng::scm
