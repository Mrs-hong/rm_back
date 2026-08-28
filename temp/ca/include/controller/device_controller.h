//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CONTROLLER_DEVICE_CONTROLLER_H
#define QIFENG_CA_INCLUDE_CONTROLLER_DEVICE_CONTROLLER_H

#include "drogon/DrObject.h"
#include "qifeng_ca/common.pb.h"
#include "qifeng_ca/device.pb.h"
#include "qifeng_ca/voiceprint.pb.h"

#include "common/http/http.h"
#include "common/status.h"
#include "core/device/device_service.h"

namespace qifeng_ca {

    class DeviceController final : public drogon::DrObject<DeviceController> {
    public:
        QIFENG_CA_METHOD_LIST_BEGIN(DeviceController);

        QIFENG_CA_METHOD_ADD(ActionDesc("获取系统信息", false), GetSystemInfo, "/sys/device/getSystemInfo",
                             drogon::Post);

        QIFENG_CA_METHOD_ADD(ActionDesc("查询设备历史", false), QueryDeviceHistory, "/sys/device/queryDeviceHistory",
                             drogon::Post);

        QIFENG_CA_METHOD_ADD("设置系统时间", UpdateSystemTime, "/sys/device/updateSystemTime", drogon::Post);

        QIFENG_CA_METHOD_ADD("设置NTP服务器", UpdateSystemNTPServer, "/sys/device/updateSystemNTPServer", drogon::Post);

        QIFENG_CA_METHOD_ADD(ActionDesc("获取NTP地址", false), GetNTPIP, "/sys/device/getNTPIP", drogon::Post);

        QIFENG_CA_METHOD_ADD(ActionDesc("检查设备空闲", false), IsDeviceIdle, "/sys/device/isDeviceIdle", drogon::Post);

        QIFENG_CA_METHOD_LIST_END;

        Status GetSystemInfo(const GetSystemInfoRequest &req, GetSystemInfoResponse &resp);

        Status QueryDeviceHistory(const QueryDeviceHistoryRequest &req, QueryDeviceHistoryResponse &resp);

        Status UpdateSystemTime(const UpdateSystemTimeRequest &req, Empty &resp);

        Status UpdateSystemNTPServer(const UpdateSystemNTPServerRequest &req, Empty &resp);

        Status GetNTPIP(const Empty &req, GetNTPIPResponse &resp);

        Status IsDeviceIdle(const Empty &req, IsDeviceIdleResponse &resp);

    private:
        DeviceService mDeviceService;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CONTROLLER_DEVICE_CONTROLLER_H
