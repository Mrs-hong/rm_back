/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/upgrades_handler.h"

#include "common/config.h"
#include "common/types.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/handler_registry.h"
#include "service_manger/key_recoder.h"
#include "service_manger/service_context.h"
#include "service_manger/upgrade_service.h"

namespace qifeng::scm {

    static std::optional<UpgradesRequest> FromJson(const Json::Value& params) {
        UpgradesRequest req;
        if (params.isMember("serviceName") && params["serviceName"].isString()) {
            req.serviceName = params["serviceName"].asString();
        }
        if (params.isMember("tarDir") && params["tarDir"].isString()) {
            req.tarDir = params["tarDir"].asString();
        }
        return req;
    }

    ScmCommand UpgradesHandler::GetCommand() const {
        return ScmCommand::UPGRADES;
    }

    ScmResponse UpgradesHandler::Handle(const ScmRequest& request,
                                        const ServiceContext& ctx,
                                        KeyOperationRecorder& recorder) {
        const auto params = FromJson(request.params);
        if (!params.has_value() || params->serviceName.empty()) {
            SLOG_WARN << "Upgrades command missing service name";
            ScmResponse response;
            response.code = -1;
            response.message = "Upgrades command requires service name";
            return response;
        }

        ScmResponse response;
        // 关键操作记录：optName="UPGRADES"，tarDir 记录外部素材路径（空表示使用服务内部soft_dir）
        recorder.RecordOperation({"UPGRADES", params->serviceName, 2, params->tarDir});
        auto result = ctx.upgradeService->PerformIntegratedUpgrade(params->serviceName, params->tarDir);
        response.code = result.code;
        response.message = result.msg;
        recorder.UpdateResult(result.IsDefaultSuccess() ? 0 : 1);
        if (result.IsDefaultSuccess()) {
            recorder.Clear();
        }
        return response;
    }

    ResultMsg UpgradesHandler::Recover(const KeyOperationRecord& record, const ServiceContext& ctx) {
        // 一体化升级未完成，检查服务状态后重试（tarDir 来自 record，可能为空表示内部 soft_dir）
        auto* svc = ctx.configLoader->GetServiceByName(record.serviceName);
        ResultMsg recoverResult;
        if (svc) {
            SLOG_INFO << "Retrying integrated upgrade for: " << record.serviceName
                      << ", tarDir: " << (record.tarDir.empty() ? "<internal>" : record.tarDir);
            recoverResult = ctx.upgradeService->PerformIntegratedUpgrade(record.serviceName, record.tarDir);
        } else {
            SLOG_INFO << "Integrated upgrade was interrupted, service not found: " << record.serviceName;
            recoverResult = MakeWarning("Integrated upgrade interrupted, service not found: " + record.serviceName);
        }
        return recoverResult;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::UPGRADES, UpgradesHandler)

}  // namespace qifeng::scm
