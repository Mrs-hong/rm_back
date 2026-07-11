/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmctl/cli_command_registry.h"

namespace qifeng::scm {

    CliCommandRegistry& CliCommandRegistry::Instance() {
        static CliCommandRegistry instance;
        return instance;
    }

    void CliCommandRegistry::Register(CommandFactory factory) {
        mFactories.push_back(std::move(factory));
    }

    std::vector<std::unique_ptr<CliCommand>> CliCommandRegistry::BuildAll() const {
        std::vector<std::unique_ptr<CliCommand>> result;
        result.reserve(mFactories.size());
        for (const auto& factory : mFactories) {
            result.push_back(factory());
        }
        return result;
    }

}  // namespace qifeng::scm
