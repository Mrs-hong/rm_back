/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/restart_all_handler.h"

#include "common/types.h"
#include "ipc/data_def.h"
#include "scmd/service_ctl.h"
#include "service_manger/key_recoder.h"

namespace qifeng::scm {

    ScmCommand RestartAllHandler::GetCommand() const {
        return ScmCommand::RESTART_ALL;
    }

    ScmResponse RestartAllHandler::Handle(const ScmRequest& /*request*/,
                                          ServiceControl& serviceControl,
                                          KeyOperationRecorder& /*recorder*/) {
        ScmResponse response;
        auto result = serviceControl.RestartAllServices();
        response.code = result.code;
        response.message = result.msg;
        return response;
    }

}  // namespace qifeng::scm
