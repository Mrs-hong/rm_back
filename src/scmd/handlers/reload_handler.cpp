/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/reload_handler.h"
#include "scmd/handler_registry.h"

#include "common/types.h"
#include "ipc/data_def.h"
#include "qifeng_framework/common/logger.h"
#include "service_manager/key_recoder.h"
#include "service_manager/service_context.h"
#include "service_manager/service_manager.h"

namespace qifeng::scm {

    ScmCommand ReloadHandler::GetCommand() const {
        return ScmCommand::RELOAD;
    }

    ScmResponse ReloadHandler::Handle(const ScmRequest& request,
                                      const ServiceContext& ctx,
                                      KeyOperationRecorder& /*recorder*/) {
        const auto* params = std::get_if<ReloadRequest>(&request.data);
        if (params == nullptr || params->serviceName.empty()) {
            SLOG_WARN << "Reload command missing service name";
            ScmResponse response;
            response.code = -1;
            response.message = "Reload command requires service name";
            return response;
        }

        ScmResponse response;
        auto result = ctx.serviceManager->ReloadService(params->serviceName);
        response.code = result.code;
        response.message = result.msg;
        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::RELOAD, ReloadHandler)

}  // namespace qifeng::scm
