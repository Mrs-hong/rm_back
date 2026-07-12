/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/upgrade_handler.h"

#include "common/config.h"
#include "common/types.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/handler_registry.h"
#include "service_manger/key_recoder.h"
#include "service_manger/service_context.h"
#include "service_manger/upgrade_service.h"

namespace qifeng::scm {

    static std::optional<UpgradeRequest> FromJson(const Json::Value& params) {
        UpgradeRequest req;
        if (params.isMember("serviceName") && params["serviceName"].isString()) {
            req.serviceName = params["serviceName"].asString();
        }
        if (params.isMember("tarDir") && params["tarDir"].isString()) {
            req.tarDir = params["tarDir"].asString();
        }
        return req;
    }

    ScmCommand UpgradeHandler::GetCommand() const {
        return ScmCommand::UPGRADE;
    }

    ScmResponse UpgradeHandler::Handle(const ScmRequest& request,
                                       const ServiceContext& ctx,
                                       KeyOperationRecorder& recorder) {
        const auto params = FromJson(request.params);
        if (!params.has_value() || params->serviceName.empty()) {
            SLOG_WARN << "Upgrade command missing service name";
            ScmResponse response;
            response.code = -1;
            response.message = "Upgrade command requires service name";
            return response;
        }

        ScmResponse response;
        recorder.RecordOperation({"upgrade", params->serviceName, 2, params->tarDir});
        auto result = ctx.upgradeService->UpdateService(params->serviceName, params->tarDir);
        if (!result.IsDefalutSuccess()) {
            response.code = result.code;
            response.message = result.msg;
            recorder.UpdateResult(1);
            return response;
        }
        auto cleanResult = ctx.upgradeService->CleanUpgradeBackup(params->serviceName);
        if (!cleanResult.IsDefalutSuccess()) {
            SLOG_WARN << "Failed to clean upgrade backup: " << cleanResult.msg;
        }
        response.code = 0;
        response.message = "success";
        recorder.UpdateResult(0);
        recorder.Clear();
        return response;
    }

    ResultMsg UpgradeHandler::Recover(const KeyOperationRecord& record, const ServiceContext& ctx) {
        // 升级未完成，检查服务状态
        auto* svc = ctx.configLoader->GetServiceByName(record.serviceName);
        ResultMsg recoverResult;
        if (svc && !record.tarDir.empty()) {
            // 服务存在且有新版本路径，尝试重新升级
            SLOG_INFO << "Retrying upgrade with tarDir: " << record.tarDir;
            recoverResult = ctx.upgradeService->UpdateService(record.serviceName, record.tarDir);
        } else {
            SLOG_INFO << "Upgrade was interrupted, manual check recommended for: " << record.serviceName;
            recoverResult = MakeWarning("Upgrade was interrupted, manual check recommended for: " + record.serviceName);
        }
        return recoverResult;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::UPGRADE, UpgradeHandler)

}  // namespace qifeng::scm
