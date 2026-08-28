//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_CONFIG_WS_CONFIG_H
#define QIFENG_CA_INCLUDE_COMMON_CONFIG_WS_CONFIG_H

#include "qifeng_framework/common/config_manager.h"

namespace qifeng_ca {

    class WsConfig {
    public:
        static WsConfig &GetInstance() {
            static WsConfig Instance;
            return Instance;
        }

        int GetHeartbeatIntervalSeconds() const {
            int interval = CONFIG_MANAGER.GetInt("websocket", "heartbeat_interval_seconds", 5);
            return (interval <= 0 || interval > 60) ? 5 : interval;
        }

        int GetPingIntervalSeconds() const {
            int interval = CONFIG_MANAGER.GetInt("websocket", "ping_interval_seconds", 30);
            return (interval <= 0 || interval > 300) ? 30 : interval;
        }

        // 普通用户(管理员/普通用户) WS 连接上限: 默认 1
        int GetNormalUserMaxConnections() const {
            int max = CONFIG_MANAGER.GetInt("websocket", "normal_user_max_connections", 1);
            return (max <= 0) ? 1 : max;
        }

        // 访客(group_id=3) WS 连接上限: 默认 10
        int GetGuestMaxConnections() const {
            int max = CONFIG_MANAGER.GetInt("websocket", "guest_max_connections", 10);
            return (max <= 0) ? 10 : max;
        }

    private:
        WsConfig() = default;
        ~WsConfig() = default;
        WsConfig(const WsConfig &) = delete;
        WsConfig &operator=(const WsConfig &) = delete;
        WsConfig(WsConfig &&) = delete;
        WsConfig &operator=(WsConfig &&) = delete;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_CONFIG_WS_CONFIG_H
