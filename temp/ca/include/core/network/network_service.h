//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_NETWORK_NETWORK_SERVICE_H
#define QIFENG_CA_INCLUDE_CORE_NETWORK_NETWORK_SERVICE_H

#include <shared_mutex>
#include <string>

#include "qifeng_ca/network.pb.h"

#include "common/status.h"
#include "core/network/dns_manager.h"
#include "core/network/interface_manager.h"
#include "core/network/netplan_manager.h"
#include "core/network/route_manager.h"

namespace qifeng_ca {

    struct NetworkInfoCache {
        std::string mIp;
        std::string mNetmask;
        std::string mGateway;
        std::vector<std::string> mDnsList;
        bool mValid = false;
    };

    class NetworkService {
    public:
        NetworkService() = default;
        ~NetworkService() = default;

        NetworkService(const NetworkService &) = delete;
        NetworkService &operator=(const NetworkService &) = delete;
        NetworkService(NetworkService &&) noexcept = delete;
        NetworkService &operator=(NetworkService &&) = delete;

        // 获取当前网络信息(IP/Netmask/Gateway/DNS), 优先从缓存获取
        Status GetNetworkInfo(GetNetworkResponse* resp);

        // 设置网络配置(静态IP + Gateway + DNS), 通过NetplanManager事务写入
        Status SetNetwork(const SetNetworkRequest &req, SetNetworkResponse* resp);

        // 重置网络配置为默认值(从NetworkConfig读取默认配置)
        Status ResetNetwork(SetNetworkResponse* resp);

    private:
        // 获取网络信息后额外查询并记录域名
        Status GetNetworkInfoV2(GetNetworkResponse* resp);

        // 设置网络后额外应用配置的域名
        Status SetNetworkV2(const SetNetworkRequest &req, SetNetworkResponse* resp);

        // 重置网络后额外恢复默认域名
        Status ResetNetworkV2(SetNetworkResponse* resp);

        // 统一的网络参数校验: 掩码合法性 + IP/网关合法性 + 同子网检查
        Status ValidateNetworkParams(const std::string &ip, const std::string &netmask, const std::string &gateway,
                                     SetNetworkResponse* resp);

        // 从各Manager实时采集网络信息(不加锁, 由调用方负责)
        Status CollectNetworkInfo();

        // 使缓存失效(在SetNetwork/ResetNetwork后调用)
        void InvalidateCache();

        // 应用 netplan 配置并失效缓存(V1 使用)
        Status ApplyNetplanConfig(const NetplanConfig &netplanCfg);

        // 网线未连接时返回专用提示, 否则返回原始错误
        Status CarrierOrError(const std::string &iface, const std::string &origMsg, int code = -1);

    private:
        InterfaceManager mInterfaceManager;
        RouteManager mRouteManager;
        DnsManager mDnsManager;
        NetplanManager mNetplanManager;
        // 全局读写锁: 读操作用读锁, 写操作用写锁
        static std::shared_mutex RwMutex;
        // GetNetworkInfo结果缓存
        static NetworkInfoCache NetCache;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_NETWORK_NETWORK_SERVICE_H
