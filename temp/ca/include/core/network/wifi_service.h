//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_NETWORK_WIFI_SERVICE_H
#define QIFENG_CA_INCLUDE_CORE_NETWORK_WIFI_SERVICE_H

#include "qifeng_ca/network.pb.h"

#include "common/status.h"
#include "core/network/wifi_manager.h"
#include "qifeng_ca/common.pb.h"

namespace qifeng_ca {

    class WifiService {
    public:
        WifiService() = default;
        ~WifiService() = default;

        WifiService(const WifiService &) = delete;
        WifiService &operator=(const WifiService &) = delete;
        WifiService(WifiService &&) = delete;
        WifiService &operator=(WifiService &&) = delete;

        Status ScanWifi(ScanWifiResponse &resp);

        Status ConnectWifi(const ConnectWifiRequest &req, ConnectWifiResponse &resp);

        Status GetWifiStatus(GetWifiStatusResponse &resp);

        Status SetDomain(const SetDomainRequest &req, SetDomainResponse &resp);

        Status GetDomain(const GetDomainRequest &req, GetDomainResponse &resp);

        Status GetApConfig(const Empty &req, GetApConfigResponse &resp);

        Status GetApClients(const Empty &req, GetApClientsResponse &resp);

        Status SetApConfig(const SetApConfigRequest &req, SetApConfigResponse &resp);

        // Wi-Fi 接口控制
        Status WifiInterfaceUp(const Empty &req, Empty &resp);
        Status WifiInterfaceDown(const Empty &req, Empty &resp);
        Status WifiDisconnect(const Empty &req, Empty &resp);

        // 热点接口控制
        Status ApInterfaceUp(const Empty &req, Empty &resp);
        Status ApInterfaceDown(const Empty &req, Empty &resp);

        // 查询 Wi-Fi 设备是否存在
        Status GetWifiDevice(const Empty &req, GetWifiDeviceResponse &resp);

    private:
        WifiManager mWifiManager;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_NETWORK_WIFI_SERVICE_H
