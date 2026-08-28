//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_DAO_DEVICE_DAO_H
#define QIFENG_CA_INCLUDE_DAO_DEVICE_DAO_H

#include <cstdint>
#include <string>
#include <vector>

#include "dao/bms_base_dao.h"
#include "dao/models/bms_device.h"

namespace qifeng_ca {

    class DeviceDao : public BmsBaseDao {
    public:
        DeviceDao() = default;
        ~DeviceDao() override = default;

        DeviceDao(const DeviceDao &) = delete;
        DeviceDao &operator=(const DeviceDao &) = delete;
        DeviceDao(DeviceDao &&) = delete;
        DeviceDao &operator=(DeviceDao &&) = delete;

        bool InsertRecord(const models::DeviceRecord &record);

        models::DeviceRecord GetLatestRecord(const std::string &deviceId);

        std::vector<models::DeviceRecord> QueryByTimeRange(const std::string &deviceId, int64_t startTime,
                                                           int64_t endTime);

        bool DeleteExpiredRecords(int64_t beforeTimestamp);

        float QueryMaxTemperature(const std::string &deviceId, const std::string &type);
};

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_DEVICE_DAO_H
