/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/core/registry.h"

#include <string>

namespace qifeng::scm {

std::vector<CheckerPtr> CheckerRegistry::BuildAll() const {
    std::vector<CheckerPtr> out;
    out.reserve(mFactories.size());
    for (const auto &kv : mFactories) {
        out.push_back(kv.second());
    }
    return out;
}

std::vector<std::string> CheckerRegistry::RegisteredNames() const {
    std::vector<std::string> names;
    names.reserve(mFactories.size());
    for (const auto &kv : mFactories) {
        names.push_back(kv.first);
    }
    return names;
}

}  // namespace qifeng::scm