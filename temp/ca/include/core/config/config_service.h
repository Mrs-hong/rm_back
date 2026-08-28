//
// Copyright (C)) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_CONFIG_CONFIG_SERVICE_H
#define QIFENG_CA_INCLUDE_CORE_CONFIG_CONFIG_SERVICE_H

#include <string_view>

#include "qifeng_ca/common.pb.h"
#include "qifeng_ca/config.pb.h"

#include "common/status.h"
#include "dao/config_dao.h"

namespace qifeng_ca {

    class ConfigService {
    public:
        ConfigService() = default;
        ~ConfigService() = default;

        ConfigService(const ConfigService &) = delete;
        ConfigService &operator=(const ConfigService &) = delete;
        ConfigService(ConfigService &&) noexcept = delete;
        ConfigService &operator=(ConfigService &&) = delete;

        Status GetWebBaseConfig(GetBaseConfigResponse* resp);

        Status SetWebBaseConfig(const SetBaseConfigRequest &req);

        Status GetSysBaseConfig(GetSysBaseConfigResponse* resp);

        Status SetSysBaseConfig(const SetSysBaseConfigRequest &req);

    private:
        std::string GetConfigJson(const std::string_view key, const std::string &defaultJson);

        bool SetConfigJson(const std::string_view key, const std::string &jsonValue);

        ConfigDao mConfigDao;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_CONFIG_CONFIG_SERVICE_H
