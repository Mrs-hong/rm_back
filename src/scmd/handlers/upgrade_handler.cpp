/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/upgrade_handler.h"

#include "common/config.h"
#include "common/types.h"
#include "ipc/data_def.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/handler_registry.h"
#include "scmd/service_ctl.h"
#include "service_manger/key_recoder.h"

namespace qifeng::scm {

    ScmCommand UpgradeHandler::GetCommand() const {
        return ScmCommand::UPGRADE;
    }

    ScmResponse UpgradeHandler::Handle(const ScmRequest& request,
                                       ServiceControl& serviceControl,
                                       KeyOperationRecorder& recorder) {
        const auto* params = std::get_if<UpgradeRequest>(&request.data);
        if (params == nullptr || params->serviceName.empty()) {
            SLOG_WARN << "Upgrade command missing service name";
            ScmResponse response;
            response.code = -1;
            response.message = "Upgrade command requires service name";
            return response;
        }

        ScmResponse response;
        recorder.RecordOperation({"upgrade", params->serviceName, 2, params->tarDir, ""});
        auto result = serviceControl.UpgradeService(params->serviceName, params->tarDir);
        response.code = result.code;
        response.message = result.msg;
        recorder.UpdateResult(result.IsDefalutSuccess() ? 0 : 1);
        if (result.IsDefalutSuccess()) {
            recorder.Clear();
        }
        return response;
    }

    ResultMsg UpgradeHandler::Recover(const KeyOperationRecord& record, ServiceControl& serviceControl) {
        // 升级未完成，检查服务状态
        const auto& configLoader = serviceControl.GetConfigLoader();
        auto* svc = configLoader.GetServiceByName(record.serviceName);
        ResultMsg recoverResult;
        if (svc && !record.tarDir.empty()) {
            // 服务存在且有新版本路径，尝试重新升级
            SLOG_INFO << "Retrying upgrade with tarDir: " << record.tarDir;
            recoverResult = serviceControl.UpgradeService(record.serviceName, record.tarDir);
        } else {
            SLOG_INFO << "Upgrade was interrupted, manual check recommended for: " << record.serviceName;
            recoverResult = MakeWarning("Upgrade was interrupted, manual check recommended for: " + record.serviceName);
        }
        return recoverResult;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::UPGRADE, UpgradeHandler)

}  // namespace qifeng::scm
