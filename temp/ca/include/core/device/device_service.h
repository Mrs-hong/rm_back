//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_DEVICE_DEVICE_SERVICE_H
#define QIFENG_CA_INCLUDE_CORE_DEVICE_DEVICE_SERVICE_H

#include <cstdint>
#include <memory>
#include <string>

#include "qifeng_ca/common.pb.h"
#include "qifeng_ca/device.pb.h"
#include "qifeng_ca/voiceprint.pb.h"

#include "common/status.h"
#include "dao/device_dao.h"
#include "dao/models/bms_device.h"

namespace qifeng_ca {

    // TODO(yf): 后续引入缓存，有下面方案：
    // 1. 分片并发查询。将一次性大范围查询改成多次小范围查询
    // 2. 分片缓存。最新查询与缓存对比只查询变更部分
    class DeviceService {
    public:
        DeviceService();
        ~DeviceService() = default;

        DeviceService(const DeviceService &) = delete;
        DeviceService &operator=(const DeviceService &) = delete;
        DeviceService(DeviceService &&) noexcept = default;
        DeviceService &operator=(DeviceService &&) noexcept = default;

        Status GetSystemInfo(const std::string &deviceId, GetSystemInfoResponse* resp);

        Status QueryDeviceHistory(const std::string &deviceId, int64_t startTime, int64_t endTime,
                                  QueryDeviceHistoryResponse* resp);

        Status UpdateSystemTime(const UpdateSystemTimeRequest &req);

        Status UpdateSystemNTPServer(const UpdateSystemNTPServerRequest &req);

        Status GetNTPIP(GetNTPIPResponse* resp);

        Status IsDeviceIdle(IsDeviceIdleResponse* resp);

    private:
        // requireSync: 预检UDP探测通过时为true(验证阶段轮询chronyc tracking直至同步完成);
        // 仅ICMP可达(疑似UDP被网络限制)时为false(只验证chrony配置加载, 同步交由chronyd指数退避重试)
        Status UpdateSystemNTPServerImpl(const UpdateSystemNTPServerRequest &req, bool requireSync);
        Status UpdateSystemTimeImpl(const UpdateSystemTimeRequest &req);

    private:
        std::shared_ptr<DeviceDao> mDao;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_DEVICE_DEVICE_SERVICE_H
