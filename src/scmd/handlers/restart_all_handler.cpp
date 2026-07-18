/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/restart_all_handler.h"
#include "scmd/handler_registry.h"

#include "common/types.h"
#include "ipc/data_def.h"
#include "scmd/service_operations.h"
#include "service_manger/key_recoder.h"
#include "service_manger/service_context.h"

namespace qifeng::scm {

    ScmCommand RestartAllHandler::GetCommand() const {
        return ScmCommand::RESTART_ALL;
    }

    ScmResponse RestartAllHandler::Handle(const ScmRequest& /*request*/,
                                          const ServiceContext& ctx,
                                          KeyOperationRecorder& /*recorder*/) {
        ScmResponse response;
        auto result = service_operations::RestartAllServices(ctx);
        response.code = result.code;
        response.message = result.msg;
        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::RESTART_ALL, RestartAllHandler)

}  // namespace qifeng::scm
