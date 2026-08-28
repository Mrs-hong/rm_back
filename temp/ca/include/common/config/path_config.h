//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_CONFIG_SERVICE_CONFIG_H
#define QIFENG_CA_INCLUDE_COMMON_CONFIG_SERVICE_CONFIG_H

#include <string>

#include "qifeng_framework/common/config_manager.h"

namespace qifeng_ca {

    class PathConfig {
    public:
        static PathConfig &GetInstance() {
            static PathConfig Instance;
            return Instance;
        }

        std::string GetServicePath() const {
            return CONFIG_MANAGER.GetString("path", "service", "config/http_config.json");
        }

        std::string GetCasbinPath() const {
            return CONFIG_MANAGER.GetString("path", "casbin", "config/rbac_model.conf");
        }

    private:
        PathConfig() = default;
        ~PathConfig() = default;
        PathConfig(const PathConfig &) = delete;
        PathConfig &operator=(const PathConfig &) = delete;
        PathConfig(PathConfig &&) = delete;
        PathConfig &operator=(PathConfig &&) = delete;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_CONFIG_SERVICE_CONFIG_H
