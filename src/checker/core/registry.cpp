/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/core/registry.h"

#include "qifeng_framework/common/logger.h"

namespace qifeng::scm {

    std::vector<std::unique_ptr<ICheckerBase>> CheckerRegistry::BuildAll(const Json::Value &root) const {
        std::vector<std::unique_ptr<ICheckerBase>> out;
        out.reserve(mFactories.size());
        for (const auto &kv : mFactories) {
            // 启用策略：json 根对象含对应 key 才实例化
            if (!root.isMember(kv.first)) {
                SLOG_INFO << "[registry] skip '" << kv.first << "': no config section";
                continue;
            }
            auto checker = kv.second();           // 工厂构造
            checker->SetConfig(root[kv.first]);   // 注入配置
            out.push_back(std::move(checker));
            SLOG_INFO << "[registry] built '" << kv.first << "'";
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
