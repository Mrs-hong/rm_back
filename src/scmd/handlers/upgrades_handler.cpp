/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/upgrades_handler.h"

#include "common/config.h"
#include "common/types.h"
#include "ipc/data_def.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/handler_registry.h"
#include "scmd/service_ctl.h"
#include "service_manger/key_recoder.h"

namespace qifeng::scm {

    ScmCommand UpgradesHandler::GetCommand() const {
        return ScmCommand::UPGRADES;
    }

    ScmResponse UpgradesHandler::Handle(const ScmRequest& request,
                                         ServiceControl& serviceControl,
                                         KeyOperationRecorder& recorder) {
        const auto* params = std::get_if<UpgradesRequest>(&request.data);
        if (params == nullptr || params->serviceName.empty()) {
            SLOG_WARN << "Upgrades command missing service name";
            ScmResponse response;
            response.code = -1;
            response.message = "Upgrades command requires service name";
            return response;
        }

        ScmResponse response;
        // 关键操作记录：optName="UPGRADES"，tarDir 记录外部素材路径（空表示使用服务内部soft_dir）
        recorder.RecordOperation({"UPGRADES", params->serviceName, 2, params->tarDir, ""});
        auto result = serviceControl.PerformIntegratedUpgrade(params->serviceName, params->tarDir);
        response.code = result.code;
        response.message = result.msg;
        recorder.UpdateResult(result.IsDefalutSuccess() ? 0 : 1);
        if (result.IsDefalutSuccess()) {
            recorder.Clear();
        }
        return response;
    }

    ResultMsg UpgradesHandler::Recover(const KeyOperationRecord& record, ServiceControl& serviceControl) {
        // 一体化升级未完成，检查服务状态后重试（tarDir 来自 record，可能为空表示内部 soft_dir）
        const auto& configLoader = serviceControl.GetConfigLoader();
        auto* svc = configLoader.GetServiceByName(record.serviceName);
        ResultMsg recoverResult;
        if (svc) {
            SLOG_INFO << "Retrying integrated upgrade for: " << record.serviceName
                      << ", tarDir: " << (record.tarDir.empty() ? "<internal>" : record.tarDir);
            recoverResult = serviceControl.PerformIntegratedUpgrade(record.serviceName, record.tarDir);
        } else {
            SLOG_INFO << "Integrated upgrade was interrupted, service not found: " << record.serviceName;
            recoverResult = MakeWarning("Integrated upgrade interrupted, service not found: " + record.serviceName);
        }
        return recoverResult;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::UPGRADES, UpgradesHandler)

}  // namespace qifeng::scm
