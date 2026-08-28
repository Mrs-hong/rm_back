//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <cstdint>
#include <string>

#include "qifeng_framework/common/logger.h"

#include "dao/config_dao.h"
#include "dao/models/bms_config.h"

namespace qifeng_ca {

    static void MapRowToSystemConfig(const soci::row &row, models::SystemConfig &cfg) {
        cfg.mId = row.get<uint64_t>(0);
        cfg.mConfigKey = row.get<std::string>(1);
        cfg.mConfigValue = row.get<std::string>(2);

        soci::indicator descInd = row.get_indicator(3);
        if (descInd == soci::i_ok) {
            cfg.mDescription = row.get<std::string>(3);
        }

        cfg.mUpdateTime = row.get<int64_t>(4);
    }

    models::SystemConfig ConfigDao::GetByKey(const std::string &key) {
        SLOG_DEBUG << "ConfigDao::GetByKey - key: " << key;
        models::SystemConfig cfg;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs =
                (session.prepare << "SELECT id, config_key, config_value, description, update_time "
                                    "FROM system_config WHERE config_key = :config_key",
                 soci::use(key, "config_key"));
            for (const soci::row &row : rs) {
                MapRowToSystemConfig(row, cfg);
                break;
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "ConfigDao::GetByKey failed: " << e.what();
        }
        return cfg;
    }

    std::vector<models::SystemConfig> ConfigDao::GetAll() {
        SLOG_DEBUG << "ConfigDao::GetAll";
        std::vector<models::SystemConfig> configs;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs =
                (session.prepare << "SELECT id, config_key, config_value, description, update_time "
                                    "FROM system_config");
            for (const soci::row &row : rs) {
                models::SystemConfig cfg;
                MapRowToSystemConfig(row, cfg);
                configs.push_back(std::move(cfg));
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "ConfigDao::GetAll failed: " << e.what();
        }
        return configs;
    }

    bool ConfigDao::Upsert(const models::SystemConfig &config) {
        SLOG_DEBUG << "ConfigDao::Upsert - key: " << config.mConfigKey;
        try {
            soci::session session = GetSession();
            session << "INSERT INTO system_config (config_key, config_value, description, update_time) "
                       "VALUES (:config_key, :config_value, :description, :update_time) "
                       "ON DUPLICATE KEY UPDATE config_value = :config_value, update_time = :update_time",
                soci::use(config.mConfigKey, "config_key"), soci::use(config.mConfigValue, "config_value"),
                soci::use(config.mDescription, "description"), soci::use(config.mUpdateTime, "update_time");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "ConfigDao::Upsert failed: " << e.what();
            return false;
        }
    }

}  // namespace qifeng_ca
