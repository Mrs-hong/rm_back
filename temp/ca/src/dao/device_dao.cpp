//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <cstdint>
#include <string>

#include "qifeng_framework/common/logger.h"

#include "dao/device_dao.h"
#include "dao/models/bms_device.h"

namespace qifeng_ca {

    constexpr const std::string_view DeviceColumns =
        "device_id, timestamp, connect, version, cpu, memory, disk, accelerator, battery, audio";

    static void MapRowToDeviceRecord(const soci::row &row, models::DeviceRecord &r) {
        r.mDeviceId = row.get<std::string>(0);
        r.mTimestamp = row.get<int64_t>(1);
        r.mConnect = row.get<std::string>(2);
        r.mVersion = row.get<std::string>(3);
        r.mCpu = row.get<std::string>(4);
        r.mMemory = row.get<std::string>(5);
        r.mDisk = row.get<std::string>(6);
        r.mAccelerator = row.get<std::string>(7);
        r.mBattery = row.get<std::string>(8);
        r.mAudio = row.get<std::string>(9);
    }

    bool DeviceDao::InsertRecord(const models::DeviceRecord &record) {
        try {
            soci::session session = GetSession();
            session << "INSERT INTO device (device_id, timestamp, connect, version, "
                       "cpu, memory, disk, accelerator, battery, audio) "
                       "VALUES (:device_id, :timestamp, :connect, :version, "
                       ":cpu, :memory, :disk, :accelerator, :battery, :audio)",
                soci::use(record.mDeviceId, "device_id"), soci::use(record.mTimestamp, "timestamp"),
                soci::use(record.mConnect, "connect"), soci::use(record.mVersion, "version"),
                soci::use(record.mCpu, "cpu"), soci::use(record.mMemory, "memory"), soci::use(record.mDisk, "disk"),
                soci::use(record.mAccelerator, "accelerator"), soci::use(record.mBattery, "battery"),
                soci::use(record.mAudio, "audio");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "DeviceDao::InsertRecord failed: " << e.what();
            return false;
        }
    }

    models::DeviceRecord DeviceDao::GetLatestRecord(const std::string &deviceId) {
        models::DeviceRecord r;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs = (session.prepare << "SELECT device_id, timestamp, connect, version, "
                                                             "cpu, memory, disk, accelerator, battery, audio "
                                                             "FROM device WHERE device_id = :device_id "
                                                             "ORDER BY timestamp DESC LIMIT 1",
                                          soci::use(deviceId, "device_id"));
            for (const soci::row &row : rs) {
                MapRowToDeviceRecord(row, r);
                break;
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "DeviceDao::GetLatestRecord failed: " << e.what();
        }
        return r;
    }

    std::vector<models::DeviceRecord> DeviceDao::QueryByTimeRange(const std::string &deviceId, int64_t startTime,
                                                                  int64_t endTime) {
        std::vector<models::DeviceRecord> result;
        try {
            soci::session session = GetSession();
            std::string sql = "SELECT device_id, timestamp, connect, version, "
                              "cpu, memory, disk, accelerator, battery, audio "
                              "FROM device WHERE timestamp >= :start_time AND timestamp <= :end_time";

            if (!deviceId.empty()) {
                sql += " AND device_id = :device_id";
            }
            sql += " ORDER BY timestamp ASC";

            soci::statement stmt = session.prepare << sql;
            stmt.exchange(soci::use(startTime, "start_time"));
            stmt.exchange(soci::use(endTime, "end_time"));
            if (!deviceId.empty()) {
                stmt.exchange(soci::use(deviceId, "device_id"));
            }

            soci::row row;
            stmt.exchange(soci::into(row));
            stmt.define_and_bind();
            stmt.execute(true);

            while (true) {
                models::DeviceRecord r;
                MapRowToDeviceRecord(row, r);
                result.push_back(std::move(r));
                if (!stmt.fetch()) {
                    break;
                }
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "DeviceDao::QueryByTimeRange failed: " << e.what();
        }
        return result;
    }

    float DeviceDao::QueryMaxTemperature(const std::string &deviceId, const std::string &type) {
        try {
            soci::session session = GetSession();
            std::string jsonPath;
            if (type == "cpu") {
                jsonPath = "$.temperature";
            } else if (type == "npu") {
                jsonPath = "$.npu.temperature";
            } else {
                return 0.0F;
            }

            std::string sql = "SELECT MAX(CAST(JSON_UNQUOTE(JSON_EXTRACT(";
            if (type == "cpu") {
                sql += "cpu";
            } else {
                sql += "accelerator";
            }
            sql += ", :json_path)) AS DECIMAL(10,2))) FROM device WHERE device_id = :device_id";

            double maxTemp = 0.0;
            soci::indicator ind;
            session << sql, soci::use(jsonPath, "json_path"), soci::use(deviceId, "device_id"),
                soci::into(maxTemp, ind);

            if (ind == soci::i_null) {
                return 0.0F;
            }
            return static_cast<float>(maxTemp);
        } catch (const std::exception &e) {
            SLOG_ERROR << "DeviceDao::QueryMaxTemperature failed: " << e.what();
            return 0.0F;
        }
    }

    bool DeviceDao::DeleteExpiredRecords(int64_t beforeTimestamp) {
        try {
            soci::session session = GetSession();
            soci::statement stmt = (session.prepare << "DELETE FROM device WHERE timestamp < :before_time",
                                    soci::use(beforeTimestamp, "before_time"));
            stmt.execute(true);
            int deleted = static_cast<int>(stmt.get_affected_rows());
            SLOG_INFO << "DeviceDao::DeleteExpiredRecords - deleted " << deleted << " records";
            return deleted >= 0;
        } catch (const std::exception &e) {
            SLOG_ERROR << "DeviceDao::DeleteExpiredRecords failed: " << e.what();
            return false;
        }
    }

}  // namespace qifeng_ca
