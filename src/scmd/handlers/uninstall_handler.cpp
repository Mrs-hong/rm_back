/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/uninstall_handler.h"
#include "scmd/handler_registry.h"

#include "common/config.h"
#include "common/types.h"
#include "ipc/data_def.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/service_operations.h"
#include "service_manager/key_recoder.h"
#include "service_manager/service_context.h"

namespace qifeng::scm {

    ScmCommand UninstallHandler::GetCommand() const {
        return ScmCommand::UNINSTALL;
    }

    ScmResponse UninstallHandler::Handle(const ScmRequest& request,
                                         const ServiceContext& ctx,
                                         KeyOperationRecorder& recorder) {
        const auto* params = std::get_if<UninstallRequest>(&request.data);
        if (params == nullptr || params->serviceName.empty()) {
            SLOG_WARN << "Uninstall command missing service name";
            ScmResponse response;
            response.code = -1;
            response.message = "Uninstall command requires service name";
            return response;
        }

        ScmResponse response;
        recorder.RecordOperation({"uninstall", params->serviceName, 2, "", ""});
        auto result = service_operations::UninstallServiceWithCleanup(ctx, params->serviceName);
        response.code = result.code;
        response.message = result.msg;
        recorder.UpdateResult(result.IsDefaultSuccess() ? 0 : 1);
        if (result.IsDefaultSuccess()) {
            recorder.Clear();
        }
        return response;
    }

    ResultMsg UninstallHandler::Recover(const KeyOperationRecord& record, const ServiceContext& ctx) {
        // 卸载未完成，检查服务是否仍存在，若存在则继续卸载
        auto* svc = ctx.configLoader->GetServiceByName(record.serviceName);
        if (svc == nullptr) {
            SLOG_INFO << "Uninstall recovery: service not found, already removed: " << record.serviceName;
            return MakeSuccess();
        }
        SLOG_INFO << "Uninstall was interrupted, retrying: " << record.serviceName;
        return service_operations::UninstallServiceWithCleanup(ctx, record.serviceName);
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::UNINSTALL, UninstallHandler)

}  // namespace qifeng::scm
