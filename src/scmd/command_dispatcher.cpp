/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/command_dispatcher.h"

#include "ipc/protocol.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/handler_registry.h"
#include "service_manger/key_recoder.h"
#include "service_manger/service_context.h"

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

    void CommandDispatcher::LoadFromRegistry(const HandlerContext& ctx) {
        auto handlers = HandlerRegistry::Instance().BuildAll(ctx);
        for (auto& handler : handlers) {
            Register(std::move(handler));
        }
    }

    ScmResponse CommandDispatcher::Dispatch(const ScmRequest& request,
                                            const ServiceContext& ctx,
                                            KeyOperationRecorder& recorder) const {
        ScmCommand cmd = request.command;
        auto it = mHandlers.find(cmd);
        if (it == mHandlers.end()) {
            ScmResponse response;
            response.code = -1;
            response.message = "Unknown command";
            SLOG_WARN << "No handler registered for command: " << ScmCommandToString(cmd);
            return response;
        }

        return it->second->Handle(request, ctx, recorder);
    }

    ResultMsg CommandDispatcher::Recover(const KeyOperationRecord& record, const ServiceContext& ctx) const {
        // 将操作名字符串映射为命令枚举
        auto cmdOpt = StringToScmCommand(record.optName);
        if (!cmdOpt.has_value()) {
            SLOG_WARN << "Unknown operation to recover: " << record.optName;
            return MakeWarning("Unknown operation: " + record.optName);
        }

        auto it = mHandlers.find(cmdOpt.value());
        if (it == mHandlers.end()) {
            SLOG_WARN << "No handler registered for recovery operation: " << record.optName;
            return MakeWarning("No handler for operation: " + record.optName);
        }

        return it->second->Recover(record, ctx);
    }

}  // namespace qifeng::scm
