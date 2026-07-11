/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/stop_all_handler.h"

#include "common/types.h"
#include "ipc/data_def.h"
#include "scmd/handler_registry.h"
#include "scmd/service_ctl.h"
#include "service_manger/key_recoder.h"

namespace qifeng::scm {

    ScmCommand StopAllHandler::GetCommand() const {
        return ScmCommand::STOP_ALL;
    }

    ScmResponse StopAllHandler::Handle(const ScmRequest& /*request*/,
                                       ServiceControl& serviceControl,
                                       KeyOperationRecorder& /*recorder*/) {
        ScmResponse response;
        auto result = serviceControl.StopAllServices();
        response.code = result.code;
        response.message = result.msg;
        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::STOP_ALL, StopAllHandler)

}  // namespace qifeng::scm
