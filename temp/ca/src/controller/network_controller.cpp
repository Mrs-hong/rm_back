//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "qifeng_framework/common/logger.h"

#include "controller/network_controller.h"

namespace qifeng_ca {

    Status NetworkController::GetNetworkInfo(const qifeng_ca::Empty &req, qifeng_ca::GetNetworkResponse &resp) {
        (void)req;
        SLOG_DEBUG << "GetNetworkInfo";

        Status status = mNetworkService.GetNetworkInfo(&resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status NetworkController::SetNetwork(const qifeng_ca::SetNetworkRequest &req, qifeng_ca::SetNetworkResponse &resp) {
        SLOG_DEBUG << "SetNetwork - ip: " << req.ip();

        Status status = mNetworkService.SetNetwork(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status NetworkController::ResetNetwork(const qifeng_ca::Empty &req, qifeng_ca::SetNetworkResponse &resp) {
        (void)req;
        SLOG_DEBUG << "ResetNetwork";

        Status status = mNetworkService.ResetNetwork(&resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status NetworkController::ScanWifi(const qifeng_ca::Empty &req, qifeng_ca::ScanWifiResponse &resp) {
        (void)req;
        SLOG_DEBUG << "NetworkController::ScanWifi";
        Status status = mWifiService.ScanWifi(resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status NetworkController::ConnectWifi(const qifeng_ca::ConnectWifiRequest &req,
                                          qifeng_ca::ConnectWifiResponse &resp) {
        SLOG_DEBUG << "NetworkController::ConnectWifi - ssid: " << req.ssid();
        Status status = mWifiService.ConnectWifi(req, resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status NetworkController::GetWifiStatus(const qifeng_ca::Empty &req, qifeng_ca::GetWifiStatusResponse &resp) {
        (void)req;
        SLOG_DEBUG << "NetworkController::GetWifiStatus";
        Status status = mWifiService.GetWifiStatus(resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status NetworkController::SetDomain(const qifeng_ca::SetDomainRequest &req, qifeng_ca::SetDomainResponse &resp) {
        SLOG_DEBUG << "NetworkController::SetDomain - domain: " << req.domain();
        Status status = mWifiService.SetDomain(req, resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status NetworkController::GetDomain(const qifeng_ca::GetDomainRequest &req, qifeng_ca::GetDomainResponse &resp) {
        (void)req;
        SLOG_DEBUG << "NetworkController::GetDomain";
        Status status = mWifiService.GetDomain(req, resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status NetworkController::GetApConfig(const qifeng_ca::Empty &req, qifeng_ca::GetApConfigResponse &resp) {
        (void)req;
        SLOG_DEBUG << "NetworkController::GetApConfig";
        Status status = mWifiService.GetApConfig(req, resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status NetworkController::GetApClients(const qifeng_ca::Empty &req, qifeng_ca::GetApClientsResponse &resp) {
        (void)req;
        SLOG_DEBUG << "NetworkController::GetApClients";
        Status status = mWifiService.GetApClients(req, resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status NetworkController::SetApConfig(const qifeng_ca::SetApConfigRequest &req,
                                          qifeng_ca::SetApConfigResponse &resp) {
        SLOG_DEBUG << "NetworkController::SetApConfig - ssid: " << req.ssid();
        Status status = mWifiService.SetApConfig(req, resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status NetworkController::WifiInterfaceDown(const qifeng_ca::Empty &req, qifeng_ca::Empty &resp) {
        (void)req;
        (void)resp;
        SLOG_DEBUG << "NetworkController::WifiInterfaceDown";
        Status status = mWifiService.WifiInterfaceDown(req, resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status NetworkController::WifiInterfaceUp(const qifeng_ca::Empty &req, qifeng_ca::Empty &resp) {
        (void)req;
        (void)resp;
        SLOG_DEBUG << "NetworkController::WifiInterfaceUp";
        Status status = mWifiService.WifiInterfaceUp(req, resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status NetworkController::WifiDisconnect(const qifeng_ca::Empty &req, qifeng_ca::Empty &resp) {
        (void)req;
        (void)resp;
        SLOG_DEBUG << "NetworkController::WifiDisconnect";
        Status status = mWifiService.WifiDisconnect(req, resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status NetworkController::ApInterfaceDown(const qifeng_ca::Empty &req, qifeng_ca::Empty &resp) {
        (void)req;
        (void)resp;
        SLOG_DEBUG << "NetworkController::ApInterfaceDown";
        Status status = mWifiService.ApInterfaceDown(req, resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status NetworkController::ApInterfaceUp(const qifeng_ca::Empty &req, qifeng_ca::Empty &resp) {
        (void)req;
        (void)resp;
        SLOG_DEBUG << "NetworkController::ApInterfaceUp";
        Status status = mWifiService.ApInterfaceUp(req, resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status NetworkController::GetWifiDevice(const qifeng_ca::Empty &req, qifeng_ca::GetWifiDeviceResponse &resp) {
        (void)req;
        SLOG_DEBUG << "NetworkController::GetWifiDevice";
        return mWifiService.GetWifiDevice(req, resp);
    }

}  // namespace qifeng_ca

QIFENG_CA_HTTP_METHOD_REGISTRY(qifeng_ca::NetworkController);
