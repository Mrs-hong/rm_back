/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/install_handler.h"

#include "common/config.h"
#include "common/types.h"
#include "ipc/data_def.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/handler_registry.h"
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
        // code=0 表示安装成功；code=1 表示安装成功但启动验证失败（警告）
        // 两种情况安装流程均已完成，应清除操作记录避免下次启动时误恢复
        bool installDone = (result.code == 0 || result.code == 1);
        recorder.UpdateResult(installDone ? 0 : 1);
        if (installDone) {
            recorder.Clear();
        }
        return response;
    }

    ResultMsg InstallHandler::Recover(const KeyOperationRecord& record, ServiceControl& serviceControl) {
        // 安装未完成，检查服务是否已存在
        const auto& configLoader = serviceControl.GetConfigLoader();
        auto* svc = configLoader.GetServiceByName(record.serviceName);
        ResultMsg recoverResult;
        if (svc) {
            // 服务已部分安装，先卸载清理再重新安装
            SLOG_INFO << "Service partially installed, cleaning up: " << record.serviceName;
            auto cleanResult = serviceControl.UninstallService(record.serviceName);
            if (!cleanResult.IsDefalutSuccess()) {
                recoverResult = cleanResult;
            } else if (!record.tarDir.empty()) {
                // 有软件包路径，可以重新安装
                SLOG_INFO << "Retrying install with tarDir: " << record.tarDir;
                recoverResult = serviceControl.Installed(record.serviceName, record.tarDir);
            } else {
                recoverResult = MakeWarning("Install interrupted but tarDir not recorded, cannot retry");
            }
        } else if (!record.tarDir.empty()) {
            // 服务不存在且有软件包路径，直接重新安装
            SLOG_INFO << "Retrying install with tarDir: " << record.tarDir;
            recoverResult = serviceControl.Installed(record.tarDir, record.serviceName);
        } else {
            recoverResult = MakeWarning("Install interrupted but tarDir not recorded, cannot retry");
        }
        return recoverResult;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::INSTALL, InstallHandler)

}  // namespace qifeng::scm
