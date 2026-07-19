/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/install_handler.h"
#include "scmd/handler_registry.h"

#include "common/config.h"
#include "common/types.h"
#include "ipc/data_def.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/service_operations.h"
#include "service_manager/key_recoder.h"
#include "service_manager/service_context.h"

namespace qifeng::scm {

    ScmCommand InstallHandler::GetCommand() const {
        return ScmCommand::INSTALL;
    }

    ScmResponse InstallHandler::Handle(const ScmRequest& request,
                                       const ServiceContext& ctx,
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
        auto result = service_operations::InstallServiceWithDb(ctx, params->serviceName, params->tarDir);
        response.code = result.code;
        response.message = result.msg;
        // code=0 表示安装成功；code=1 表示安装成功但启动验证失败（警告）
        // 两种情况安装流程均已完成，应清除操作记录避免下次启动时误恢复
        bool installDone = (result.code == 0 || result.code == 1);
        recorder.UpdateResult(installDone ? 0 : 1);
        if (installDone) {
            recorder.Clear();
        }
        return response;
    }

    ResultMsg InstallHandler::Recover(const KeyOperationRecord& record, const ServiceContext& ctx) {
        // 安装未完成，检查服务是否已存在
        auto* svc = ctx.configLoader->GetServiceByName(record.serviceName);
        if (svc) {
            // 服务已部分安装，先卸载清理再重新安装
            SLOG_INFO << "Service partially installed, cleaning up: " << record.serviceName;
            auto cleanResult = service_operations::UninstallServiceWithCleanup(ctx, record.serviceName);
            if (!cleanResult.IsDefaultSuccess()) {
                return cleanResult;
            }
            if (record.tarDir.empty()) {
                return MakeWarning("Install interrupted but tarDir not recorded, cannot retry");
            }
            SLOG_INFO << "Retrying install with tarDir: " << record.tarDir;
            return service_operations::InstallServiceWithDb(ctx, record.serviceName, record.tarDir);
        }
        if (record.tarDir.empty()) {
            return MakeWarning("Install interrupted but tarDir not recorded, cannot retry");
        }
        SLOG_INFO << "Retrying install with tarDir: " << record.tarDir;
        return service_operations::InstallServiceWithDb(ctx, record.serviceName, record.tarDir);
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::INSTALL, InstallHandler)

}  // namespace qifeng::scm
