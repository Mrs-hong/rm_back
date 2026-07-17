/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/command_dispatcher.h"

#include "ipc/data_def.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/service_ctl.h"
#include "service_manger/key_recoder.h"

namespace qifeng::scm {

    void CommandDispatcher::Register(std::unique_ptr<ICommandHandler> handler) {
        if (!handler) {
            SLOG_ERROR << "Attempted to register null command handler";
            return;
        }

        ScmCommand cmd = handler->GetCommand();
        if (mHandlers.find(cmd) != mHandlers.end()) {
            SLOG_ERROR << "Command handler already registered: " << ScmCommandToString(cmd);
            return;
        }

        mHandlers.emplace(cmd, std::move(handler));
    }

    ScmResponse CommandDispatcher::Dispatch(const ScmRequest& request,
                                            ServiceControl& serviceControl,
                                            KeyOperationRecorder& recorder) const {
        ScmCommand cmd = request.Command();
        auto it = mHandlers.find(cmd);
        if (it == mHandlers.end()) {
            ScmResponse response;
            response.code = -1;
            response.message = "Unknown command";
            SLOG_WARN << "No handler registered for command: " << ScmCommandToString(cmd);
            return response;
        }

        return it->second->Handle(request, serviceControl, recorder);
    }

}  // namespace qifeng::scm
