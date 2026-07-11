/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handler_registry.h"

namespace qifeng::scm {

    HandlerRegistry& HandlerRegistry::Instance() {
        static HandlerRegistry instance;
        return instance;
    }

    void HandlerRegistry::Register(ScmCommand cmd, HandlerFactory factory) {
        mFactories.emplace_back(cmd, std::move(factory));
    }

    std::vector<std::unique_ptr<ICommandHandler>> HandlerRegistry::BuildAll(const HandlerContext& ctx) const {
        std::vector<std::unique_ptr<ICommandHandler>> result;
        result.reserve(mFactories.size());
        for (const auto& [cmd, factory] : mFactories) {
            (void)cmd;
            result.push_back(factory(ctx));
        }
        return result;
    }

}  // namespace qifeng::scm
