/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/stop_all_handler.h"

#include "common/types.h"
#include "scmd/handler_registry.h"
#include "service_manger/key_recoder.h"
#include "service_manger/service_context.h"
#include "service_manger/service_manager.h"

namespace qifeng::scm {

    ScmCommand StopAllHandler::GetCommand() const {
        return ScmCommand::STOP_ALL;
    }

    ScmResponse StopAllHandler::Handle(const ScmRequest& /*request*/,
                                       const ServiceContext& ctx,
                                       KeyOperationRecorder& /*recorder*/) {
        ScmResponse response;
        auto result = ctx.serviceManager->StopAllServices();
        response.code = result.code;
        response.message = result.msg;
        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::STOP_ALL, StopAllHandler)

}  // namespace qifeng::scm
