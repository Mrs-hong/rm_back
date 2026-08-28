//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_CONFIG_SERVICE_CONFIG_H
#define QIFENG_CA_INCLUDE_COMMON_CONFIG_SERVICE_CONFIG_H

#include <string>

#include "qifeng_framework/common/config_manager.h"

namespace qifeng_ca {

    class ServiceConfig {
    public:
        static ServiceConfig &GetInstance() {
            static ServiceConfig Instance;
            return Instance;
        }

        std::string GetJsonPath() const {
            return CONFIG_MANAGER.GetString("service.api_service", "json_path", "config/http_config.json");
        }

        std::string GetName() const { return CONFIG_MANAGER.GetString("service.api_service", "name", ""); }

        std::string GetRegisterType() const {
            return CONFIG_MANAGER.GetString("service.api_service", "register_type", "local");
        }

    private:
        ServiceConfig() = default;
        ~ServiceConfig() = default;
        ServiceConfig(const ServiceConfig &) = delete;
        ServiceConfig &operator=(const ServiceConfig &) = delete;
        ServiceConfig(ServiceConfig &&) = delete;
        ServiceConfig &operator=(ServiceConfig &&) = delete;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_CONFIG_SERVICE_CONFIG_H
