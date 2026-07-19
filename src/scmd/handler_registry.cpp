/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handler_registry.h"

#include "qifeng_framework/common/logger.h"

namespace qifeng::scm {

    HandlerRegistry &HandlerRegistry::Instance() {
        // Meyers 单例：C++11 起局部静态变量线程安全初始化
        static HandlerRegistry Instance;
        return Instance;
    }

    void HandlerRegistry::Register(ScmCommand cmd, HandlerFactory factory) {
        if (!factory) {
            SLOG_ERROR << "Attempted to register null factory for command: " << ScmCommandToString(cmd);
            return;
        }
        mFactories.emplace_back(cmd, std::move(factory));
    }

    std::vector<std::unique_ptr<ICommandHandler>> HandlerRegistry::BuildAll(const HandlerContext &ctx) const {
        std::vector<std::unique_ptr<ICommandHandler>> handlers;
        handlers.reserve(mFactories.size());
        for (const auto &[cmd, factory] : mFactories) {
            auto handler = factory(ctx);
            if (handler == nullptr) {
                SLOG_ERROR << "Factory returned null handler for command: " << ScmCommandToString(cmd);
                continue;
            }
            handlers.push_back(std::move(handler));
        }
        return handlers;
    }

}  // namespace qifeng::scm
