//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "controller/device_controller.h"

namespace qifeng_ca {

    Status DeviceController::GetSystemInfo(const GetSystemInfoRequest &req, GetSystemInfoResponse &resp) {
        std::string deviceId = req.has_device_id() ? req.device_id() : "";
        Status status = mDeviceService.GetSystemInfo(deviceId, &resp);
        if (status.GetCode()) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status DeviceController::QueryDeviceHistory(const QueryDeviceHistoryRequest &req,
                                                QueryDeviceHistoryResponse &resp) {
        std::string deviceId = req.has_device_id() ? req.device_id() : "";
        Status status = mDeviceService.QueryDeviceHistory(deviceId, req.start_time(), req.end_time(), &resp);
        if (status.GetCode()) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status DeviceController::UpdateSystemTime(const UpdateSystemTimeRequest &req, Empty &resp) {
        (void)resp;
        Status status = mDeviceService.UpdateSystemTime(req);
        if (status.GetCode()) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status DeviceController::UpdateSystemNTPServer(const UpdateSystemNTPServerRequest &req, Empty &resp) {
        (void)resp;
        Status status = mDeviceService.UpdateSystemNTPServer(req);
        if (status.GetCode()) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status DeviceController::GetNTPIP(const Empty &req, GetNTPIPResponse &resp) {
        (void)req;
        Status status = mDeviceService.GetNTPIP(&resp);
        if (status.GetCode()) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status DeviceController::IsDeviceIdle(const Empty &req, IsDeviceIdleResponse &resp) {
        (void)req;
        SLOG_DEBUG << "IsDeviceIdle";

        Status status = mDeviceService.IsDeviceIdle(&resp);
        if (status.GetCode()) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

}  // namespace qifeng_ca
QIFENG_CA_HTTP_METHOD_REGISTRY(qifeng_ca::DeviceController);
