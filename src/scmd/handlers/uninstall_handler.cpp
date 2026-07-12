/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/uninstall_handler.h"

#include "common/config.h"
#include "common/types.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/handler_registry.h"
#include "scmd/service_operations.h"
#include "service_manger/key_recoder.h"
#include "service_manger/service_context.h"

namespace qifeng::scm {

    static std::optional<UninstallRequest> FromJson(const Json::Value& params) {
        UninstallRequest req;
        if (params.isMember("serviceName") && params["serviceName"].isString()) {
            req.serviceName = params["serviceName"].asString();
        }
        return req;
    }

    ScmCommand UninstallHandler::GetCommand() const {
        return ScmCommand::UNINSTALL;
    }

    ScmResponse UninstallHandler::Handle(const ScmRequest& request,
                                         const ServiceContext& ctx,
                                         KeyOperationRecorder& recorder) {
        auto paramsOpt = FromJson(request.params);
        if (!paramsOpt || paramsOpt->serviceName.empty()) {
            SLOG_WARN << "Uninstall command missing service name";
            ScmResponse response;
            response.code = -1;
            response.message = "Uninstall command requires service name";
            return response;
        }

        const auto& params = *paramsOpt;
        ScmResponse response;
        recorder.RecordOperation({"uninstall", params.serviceName, 2, ""});
        auto result = UninstallServiceWithCleanup(ctx, params.serviceName);
        response.code = result.code;
        response.message = result.msg;
        recorder.UpdateResult(result.IsDefaultSuccess() ? 0 : 1);
        if (result.IsDefaultSuccess()) {
            recorder.Clear();
        }
        return response;
    }

    ResultMsg UninstallHandler::Recover(const KeyOperationRecord& record, const ServiceContext& ctx) {
        // 卸载未完成，尝试继续卸载
        auto* svc = ctx.configLoader->GetServiceByName(record.serviceName);
        ResultMsg recoverResult;
        if (svc) {
            SLOG_INFO << "Uninstall was interrupted, retrying: " << record.serviceName;
            recoverResult = UninstallServiceWithCleanup(ctx, record.serviceName);
        } else {
            recoverResult = MakeSuccess();
        }
        return recoverResult;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::UNINSTALL, UninstallHandler)

}  // namespace qifeng::scm
