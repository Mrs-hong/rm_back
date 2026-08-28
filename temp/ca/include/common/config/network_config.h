//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_CONFIG_NETWORK_CONFIG_H
#define QIFENG_CA_INCLUDE_COMMON_CONFIG_NETWORK_CONFIG_H

#include "qifeng_framework/common/config_manager.h"

namespace qifeng_ca {

    class NetworkConfig {
    public:
        static NetworkConfig &GetInstance() {
            static NetworkConfig Instance;
            return Instance;
        }

        std::string GetDefaultInterface() const {
            return CONFIG_MANAGER.GetString("network", "default_interface", "eth0");
        }

        std::string GetDefaultIp() const { return CONFIG_MANAGER.GetString("network", "default_ip", "192.168.1.100"); }

        std::string GetDefaultNetmask() const {
            return CONFIG_MANAGER.GetString("network", "default_netmask", "255.255.255.0");
        }

        std::string GetDefaultGateway() const {
            return CONFIG_MANAGER.GetString("network", "default_gateway", "192.168.1.1");
        }

        std::string GetDefaultDns() const {
            return CONFIG_MANAGER.GetString("network", "default_dns", "8.8.8.8,8.8.4.4");
        }

        std::string GetNetplanConfigPath() const {
            return CONFIG_MANAGER.GetString("network", "netplan_config_path", "/etc/netplan/01-netcfg.yaml");
        }

        // 功能版本开关: "v1"(默认, 当前版本) 或 "v2"(改动后版本)
        std::string GetFeatureVersion() const { return CONFIG_MANAGER.GetString("network", "feature_version", "v2"); }

        // 设备是否支持 Wi-Fi/热点功能(不支持的设备对应接口直接返回不支持, 前端置灰)
        bool IsWifiSupported() const { return CONFIG_MANAGER.GetBool("network", "wifi_supported", false); }

    private:
        NetworkConfig() = default;
        ~NetworkConfig() = default;
        NetworkConfig(const NetworkConfig &) = delete;
        NetworkConfig &operator=(const NetworkConfig &) = delete;
        NetworkConfig(NetworkConfig &&) = delete;
        NetworkConfig &operator=(NetworkConfig &&) = delete;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_CONFIG_NETWORK_CONFIG_H
