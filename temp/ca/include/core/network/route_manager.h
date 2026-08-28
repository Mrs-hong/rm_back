//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_NETWORK_ROUTE_MANAGER_H
#define QIFENG_CA_INCLUDE_CORE_NETWORK_ROUTE_MANAGER_H

#include <string>
#include <vector>

namespace qifeng_ca {

    struct RouteEntry {
        std::string mInterface;
        std::string mDestination;
        std::string mGateway;
        int mPriority = 0;
    };

    class RouteManager {
    public:
        RouteManager() = default;
        ~RouteManager() = default;

        RouteManager(const RouteManager &) = delete;
        RouteManager &operator=(const RouteManager &) = delete;
        RouteManager(RouteManager &&) = delete;
        RouteManager &operator=(RouteManager &&) = delete;

        // 获取默认网关(优先rtnetlink, fallback /proc/net/route)
        bool GetDefaultGateway(const std::string &iface, std::string &outGateway);

        // 获取所有路由条目
        std::vector<RouteEntry> GetAllRoutes();

    private:
        static bool ParseRouteFile(const std::string &iface, std::string &outGateway);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_NETWORK_ROUTE_MANAGER_H
