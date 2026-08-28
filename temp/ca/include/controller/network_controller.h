//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CONTROLLER_NETWORK_CONTROLLER_H
#define QIFENG_CA_INCLUDE_CONTROLLER_NETWORK_CONTROLLER_H

#include "drogon/DrObject.h"
#include "qifeng_ca/network.pb.h"

#include "common/http/http.h"
#include "common/status.h"
#include "core/network/network_service.h"
#include "core/network/wifi_service.h"
#include "qifeng_ca/common.pb.h"

namespace qifeng_ca {

    class NetworkController final : public drogon::DrObject<NetworkController> {
    public:
        QIFENG_CA_METHOD_LIST_BEGIN(NetworkController);

        QIFENG_CA_METHOD_ADD(ActionDesc("获取网络信息", false), GetNetworkInfo, "/sys/network/getNetwork", drogon::Get);

        QIFENG_CA_METHOD_ADD("修改网络配置", SetNetwork, "/sys/network/setNetwork", drogon::Post);

        QIFENG_CA_METHOD_ADD("重置网络配置", ResetNetwork, "/sys/network/resetNetwork", drogon::Post);

        QIFENG_CA_METHOD_ADD(ActionDesc("扫描Wi-Fi列表", false), ScanWifi, "/sys/wifi/scan", drogon::Get);

        QIFENG_CA_METHOD_ADD("连接Wi-Fi", ConnectWifi, "/sys/wifi/connect", drogon::Post);

        QIFENG_CA_METHOD_ADD(ActionDesc("查询Wi-Fi状态", false), GetWifiStatus, "/sys/wifi/status", drogon::Get);

        QIFENG_CA_METHOD_ADD("设置域名", SetDomain, "/sys/network/setDomain", drogon::Post);

        QIFENG_CA_METHOD_ADD(ActionDesc("查询域名", false), GetDomain, "/sys/network/getDomain", drogon::Get);

        QIFENG_CA_METHOD_ADD(ActionDesc("查询热点配置", false), GetApConfig, "/sys/ap/config", drogon::Get);

        QIFENG_CA_METHOD_ADD(ActionDesc("查询热点连接设备", false), GetApClients, "/sys/ap/clients", drogon::Get);

        QIFENG_CA_METHOD_ADD("修改热点配置", SetApConfig, "/sys/ap/setConfig", drogon::Post);

        QIFENG_CA_METHOD_ADD("关闭Wi-Fi", WifiInterfaceDown, "/sys/wifi/down", drogon::Post);

        QIFENG_CA_METHOD_ADD("开启Wi-Fi", WifiInterfaceUp, "/sys/wifi/up", drogon::Post);

        QIFENG_CA_METHOD_ADD("断开Wi-Fi连接", WifiDisconnect, "/sys/wifi/disconnect", drogon::Post);

        QIFENG_CA_METHOD_ADD("关闭热点", ApInterfaceDown, "/sys/ap/down", drogon::Post);

        QIFENG_CA_METHOD_ADD("开启热点", ApInterfaceUp, "/sys/ap/up", drogon::Post);

        QIFENG_CA_METHOD_ADD(ActionDesc("查询Wi-Fi设备", false), GetWifiDevice, "/sys/wifi/device", drogon::Get);

        QIFENG_CA_METHOD_LIST_END;

        Status GetNetworkInfo(const Empty &req, GetNetworkResponse &resp);

        Status SetNetwork(const SetNetworkRequest &req, SetNetworkResponse &resp);

        Status ResetNetwork(const Empty &req, SetNetworkResponse &resp);

        Status ScanWifi(const Empty &req, ScanWifiResponse &resp);

        Status ConnectWifi(const ConnectWifiRequest &req, ConnectWifiResponse &resp);

        Status GetWifiStatus(const Empty &req, GetWifiStatusResponse &resp);

        Status SetDomain(const SetDomainRequest &req, SetDomainResponse &resp);

        Status GetDomain(const GetDomainRequest &req, GetDomainResponse &resp);

        Status GetApConfig(const Empty &req, GetApConfigResponse &resp);

        Status GetApClients(const Empty &req, GetApClientsResponse &resp);

        Status SetApConfig(const SetApConfigRequest &req, SetApConfigResponse &resp);

        Status WifiInterfaceDown(const Empty &req, Empty &resp);

        Status WifiInterfaceUp(const Empty &req, Empty &resp);

        Status WifiDisconnect(const Empty &req, Empty &resp);

        Status ApInterfaceDown(const Empty &req, Empty &resp);

        Status ApInterfaceUp(const Empty &req, Empty &resp);

        Status GetWifiDevice(const Empty &req, GetWifiDeviceResponse &resp);

    private:
        NetworkService mNetworkService;
        WifiService mWifiService;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CONTROLLER_NETWORK_CONTROLLER_H
