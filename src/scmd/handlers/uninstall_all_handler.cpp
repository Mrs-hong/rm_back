/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/uninstall_all_handler.h"

#include "common/config.h"
#include "common/types.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/handler_registry.h"
#include "scmd/service_operations.h"
#include "service_manger/key_recoder.h"
#include "service_manger/service_context.h"

namespace qifeng::scm {

    ScmCommand UninstallAllHandler::GetCommand() const {
        return ScmCommand::UNINSTALL_ALL;
    }

    ScmResponse UninstallAllHandler::Handle(const ScmRequest& /*request*/,
                                            const ServiceContext& ctx,
                                            KeyOperationRecorder& /*recorder*/) {
        SLOG_INFO << "Uninstalling all services";

        // 获取所有已安装服务，按逆序逐个卸载（后安装的先卸载，尽量保证依赖关系正确）
        auto allServices = ctx.configLoader->GetAllServices();
        ResultMsg result;
        if (allServices.empty()) {
            SLOG_INFO << "No services installed, nothing to uninstall";
            result = MakeSuccess();
        } else {
            int failCount = 0;
            std::string lastError;
            for (auto it = allServices.rbegin(); it != allServices.rend(); ++it) {
                const auto& svcName = it->serviceName;
                SLOG_INFO << "Uninstalling service: " << svcName;
                auto uninstallResult = UninstallServiceWithCleanup(ctx, svcName);
                if (!uninstallResult.IsDefalutSuccess()) {
                    ++failCount;
                    lastError = svcName + ": " + uninstallResult.msg;
                    SLOG_WARN << "Failed to uninstall service " << svcName << ": " << uninstallResult.msg;
                }
            }
            if (failCount > 0) {
                result = MakeWarning("Uninstalled " + std::to_string(allServices.size() - static_cast<size_t>(failCount))
                                     + "/" + std::to_string(allServices.size()) + " services, last error: " + lastError);
            } else {
                SLOG_INFO << "All services uninstalled successfully";
                result = MakeSuccess();
            }
        }

        ScmResponse response;
        response.code = result.code;
        response.message = result.msg;
        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::UNINSTALL_ALL, UninstallAllHandler)

}  // namespace qifeng::scm
