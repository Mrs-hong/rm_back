//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <vector>

#include "qifeng_framework/common/logger.h"

#include "common/config/network_config.h"
#include "core/network/wifi_service.h"

namespace qifeng_ca {

    namespace {
        // 设备是否支持 Wi-Fi/热点功能(由配置开关控制, 不支持的设备前端置灰)
        bool IsWifiSupported() {
            return NetworkConfig::GetInstance().IsWifiSupported();
        }

        // 设备不支持 Wi-Fi/热点时的统一错误
        Status WifiNotSupported() {
            return Status {-1, "设备不支持Wi-Fi/热点功能"};
        }
    }  // namespace

    Status WifiService::ScanWifi(ScanWifiResponse &resp) {
        if (!IsWifiSupported()) {
            return WifiNotSupported();
        }
        std::vector<WifiScanItem> list;
        Status status = mWifiManager.ScanWifi(list);
        if (!status.IsSuccess()) {
            FLOG_ERROR(status.ToString());
            return status;
        }
        for (const auto &info : list) {
            auto* network = resp.add_networks();
            network->set_ssid(info.mSsid);
            network->set_signal_dbm(info.mSignalDbm);
        }
        return Status {};
    }

    Status WifiService::ConnectWifi(const ConnectWifiRequest &req, ConnectWifiResponse &resp) {
        if (!IsWifiSupported()) {
            return WifiNotSupported();
        }
        Status status = mWifiManager.ConnectWifi(req.ssid(), req.password());
        resp.set_success(status.IsSuccess());
        resp.set_message(status.IsSuccess() ? "Wi-Fi 连接已触发" : status.GetMsg());
        if (!status.IsSuccess()) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status WifiService::GetWifiStatus(GetWifiStatusResponse &resp) {
        if (!IsWifiSupported()) {
            return WifiNotSupported();
        }
        WifiConnectionStatus wifiStatus;
        Status status = mWifiManager.GetWifiStatus(wifiStatus);
        if (!status.IsSuccess()) {
            FLOG_ERROR(status.ToString());
            return status;
        }
        resp.set_ssid(wifiStatus.mSsid);
        resp.set_key_mgmt(wifiStatus.mKeyMgmt);
        resp.set_wpa_state(wifiStatus.mWpaState);
        resp.set_ip_address(wifiStatus.mIpAddress);
        resp.set_connected(wifiStatus.mConnected);
        resp.set_interface_down(wifiStatus.mInterfaceDown);
        return Status {};
    }

    Status WifiService::SetDomain(const SetDomainRequest &req, SetDomainResponse &resp) {
        Status status = mWifiManager.SetDomain(req.domain());
        resp.set_success(status.IsSuccess());
        resp.set_message(status.IsSuccess() ? "域名设置成功" : status.GetMsg());
        if (!status.IsSuccess()) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status WifiService::GetDomain(const GetDomainRequest &req, GetDomainResponse &resp) {
        (void)req;
        std::string domain;
        Status status = mWifiManager.GetDomain(domain);
        if (!status.IsSuccess()) {
            FLOG_ERROR(status.ToString());
            return status;
        }
        resp.set_domain(domain);
        return Status {};
    }

    Status WifiService::GetApConfig(const Empty &req, GetApConfigResponse &resp) {
        (void)req;
        if (!IsWifiSupported()) {
            return WifiNotSupported();
        }
        ApConfigInfo config;
        Status status = mWifiManager.GetApConfig(config);
        if (!status.IsSuccess()) {
            FLOG_ERROR(status.ToString());
            return status;
        }
        resp.set_ssid(config.mSsid);
        resp.set_password(config.mPassword);
        resp.set_ap_enabled(config.mApEnabled);
        return Status {};
    }

    Status WifiService::GetApClients(const Empty &req, GetApClientsResponse &resp) {
        (void)req;
        if (!IsWifiSupported()) {
            return WifiNotSupported();
        }
        ApClientsInfo info;
        Status status = mWifiManager.GetApClients(info);
        if (!status.IsSuccess()) {
            FLOG_ERROR(status.ToString());
            return status;
        }
        resp.set_client_count(info.mClientCount);
        return Status {};
    }

    Status WifiService::SetApConfig(const SetApConfigRequest &req, SetApConfigResponse &resp) {
        if (!IsWifiSupported()) {
            return WifiNotSupported();
        }
        Status status = mWifiManager.SetApConfig(req.ssid(), req.password());
        if (status.IsSuccess()) {
            // 配置成功后自动触发应用
            Status applyStatus = mWifiManager.ApplyApConfig();
            if (applyStatus.IsSuccess()) {
                return {};
            } else {
                SLOG_ERROR << "SetApConfig: apply failed, " << applyStatus.ToString();
                return {-1, "热点配置失败"};
            }
        }
        return status;
    }

    Status WifiService::WifiInterfaceUp(const Empty &req, Empty &resp) {
        (void)req;
        (void)resp;
        if (!IsWifiSupported()) {
            return WifiNotSupported();
        }
        return mWifiManager.WifiInterfaceUp();
    }

    Status WifiService::WifiInterfaceDown(const Empty &req, Empty &resp) {
        (void)req;
        (void)resp;
        if (!IsWifiSupported()) {
            return WifiNotSupported();
        }
        return mWifiManager.WifiInterfaceDown();
    }

    Status WifiService::WifiDisconnect(const Empty &req, Empty &resp) {
        (void)req;
        (void)resp;
        if (!IsWifiSupported()) {
            return WifiNotSupported();
        }
        return mWifiManager.WifiDisconnect();
    }

    Status WifiService::ApInterfaceUp(const Empty &req, Empty &resp) {
        (void)req;
        (void)resp;
        if (!IsWifiSupported()) {
            return WifiNotSupported();
        }
        return mWifiManager.ApInterfaceUp();
    }

    Status WifiService::ApInterfaceDown(const Empty &req, Empty &resp) {
        (void)req;
        (void)resp;
        if (!IsWifiSupported()) {
            return WifiNotSupported();
        }
        return mWifiManager.ApInterfaceDown();
    }

    Status WifiService::GetWifiDevice(const Empty &req, GetWifiDeviceResponse &resp) {
        (void)req;
        // 设备不支持 Wi-Fi/热点时直接返回不存在, 前端据此置灰相关功能
        if (!IsWifiSupported()) {
            resp.set_exists(false);
            return Status {};
        }
        resp.set_exists(mWifiManager.HasWifiDevice());
        return Status {};
    }

}  // namespace qifeng_ca
