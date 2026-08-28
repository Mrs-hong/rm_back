//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CONTROLLER_CONFIG_CONTROLLER_H
#define QIFENG_CA_INCLUDE_CONTROLLER_CONFIG_CONTROLLER_H

#include "common/audit_action_registry.h"
#include "drogon/DrObject.h"
#include "qifeng_ca/common.pb.h"
#include "qifeng_ca/config.pb.h"

#include "common/http/http.h"
#include "common/status.h"
#include "core/config/config_service.h"

namespace qifeng_ca {

    class ConfigController final : public drogon::DrObject<ConfigController> {
    public:
        QIFENG_CA_METHOD_LIST_BEGIN(ConfigController);

        QIFENG_CA_METHOD_ADD(ActionDesc("获取用户配置", false), GetWebBaseConfig, "/web/config/getBaseConfig",
                             drogon::Post);

        QIFENG_CA_METHOD_ADD("设置用户配置", SetWebBaseConfig, "/web/config/setBaseConfig", drogon::Post);

        QIFENG_CA_METHOD_ADD(ActionDesc("获取系统配置", false), GetSysBaseConfig, "/sys/config/getBaseConfig",
                             drogon::Post);

        QIFENG_CA_METHOD_ADD("设置系统配置", SetSysBaseConfig, "/sys/config/setBaseConfig", drogon::Post);

        QIFENG_CA_METHOD_LIST_END;

        Status GetWebBaseConfig(const Empty &req, GetBaseConfigResponse &resp);

        Status SetWebBaseConfig(const SetBaseConfigRequest &req, Empty &resp);

        Status GetSysBaseConfig(const Empty &req, GetSysBaseConfigResponse &resp);

        Status SetSysBaseConfig(const SetSysBaseConfigRequest &req, Empty &resp);

    private:
        ConfigService mConfigService;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CONTROLLER_CONFIG_CONTROLLER_H
