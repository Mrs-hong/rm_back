/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/uninstall_all_handler.h"

#include "common/types.h"
#include "ipc/data_def.h"
#include "scmd/handler_registry.h"
#include "scmd/service_ctl.h"
#include "service_manger/key_recoder.h"

namespace qifeng::scm {

    ScmCommand UninstallAllHandler::GetCommand() const {
        return ScmCommand::UNINSTALL_ALL;
    }

    ScmResponse UninstallAllHandler::Handle(const ScmRequest& /*request*/,
                                            ServiceControl& serviceControl,
                                            KeyOperationRecorder& /*recorder*/) {
        ScmResponse response;
        auto result = serviceControl.UninstallAllServices();
        response.code = result.code;
        response.message = result.msg;
        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::UNINSTALL_ALL, UninstallAllHandler)

}  // namespace qifeng::scm
