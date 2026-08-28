//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

// InterfaceManager: 网卡接口信息管理器
// 职责: 通过单次getifaddrs扫描获取所有网卡IPv4信息, 避免重复扫描
// 设计: 每次调用ScanInterfaces()刷新缓存, 不持有长期缓存(网络状态可能变化)

#ifndef QIFENG_CA_INCLUDE_CORE_NETWORK_INTERFACE_MANAGER_H
#define QIFENG_CA_INCLUDE_CORE_NETWORK_INTERFACE_MANAGER_H

#include <cstdint>
#include <string>
#include <vector>

namespace qifeng_ca {

    // 单个网卡的IPv4信息
    struct InterfaceInfo {
        std::string mName;     // 接口名, 如 eth0, enp3s0
        std::string mIpv4;     // IPv4地址, 如 192.168.1.100
        std::string mNetmask;  // 子网掩码, 如 255.255.255.0
    };

    class InterfaceManager {
    public:
        InterfaceManager() = default;
        ~InterfaceManager() = default;

        InterfaceManager(const InterfaceManager &) = delete;
        InterfaceManager &operator=(const InterfaceManager &) = delete;
        InterfaceManager(InterfaceManager &&) = delete;
        InterfaceManager &operator=(InterfaceManager &&) = delete;

        // 获取指定网卡的接口信息(IPv4 + Netmask)
        bool GetInterfaceInfo(const std::string &iface, InterfaceInfo &outInfo);

        // 获取所有已启用IPv4的网卡信息
        std::vector<InterfaceInfo> GetAllInterfaces();

        // 检查指定网卡是否存在且已启用IPv4
        bool IsInterfaceUp(const std::string &iface);

        // 获取指定网卡的CIDR前缀长度
        bool GetInterfaceCidr(const std::string &iface, int &outCidr);

        // 检测网卡物理连接状态(carrier)
        bool IsCarrierUp(const std::string &iface);

    private:
        // 执行getifaddrs扫描, 结果存入mCachedInterfaces
        bool ScanInterfaces();

        // 校验接口名合法性(防止命令注入)
        static bool IsValidInterfaceName(const std::string &iface);

    private:
        std::vector<InterfaceInfo> mCachedInterfaces;
        bool mScanned = false;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_NETWORK_INTERFACE_MANAGER_H
