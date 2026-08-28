//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_CONFIG_DATABASE_CONFIG_H
#define QIFENG_CA_INCLUDE_COMMON_CONFIG_DATABASE_CONFIG_H

#include <cstdint>
#include <string>

#include "qifeng_framework/common/config_manager.h"

namespace qifeng_ca {

    class DatabaseConfig {
    public:
        static DatabaseConfig &GetInstance() {
            static DatabaseConfig Instance;
            return Instance;
        }

        std::string GetDbType() const { return CONFIG_MANAGER.GetString("database", "db_type", "mysql"); }

        std::string GetMysqlServer() const { return CONFIG_MANAGER.GetString("database", "mysql_server", "127.0.0.1"); }

        int32_t GetMysqlPort() const { return CONFIG_MANAGER.GetInt("database", "mysql_port", 3306); }

        std::string GetMysqlDb() const { return CONFIG_MANAGER.GetString("database", "mysql_db", ""); }

        std::string GetMysqlUser() const;

        std::string GetMysqlPassword() const;

        int32_t GetMysqlPoolSize() const { return CONFIG_MANAGER.GetInt("database", "mysql_pool_size", 10); }

    private:
        DatabaseConfig() = default;
        ~DatabaseConfig() = default;
        DatabaseConfig(const DatabaseConfig &) = delete;
        DatabaseConfig &operator=(const DatabaseConfig &) = delete;
        DatabaseConfig(DatabaseConfig &&) = delete;
        DatabaseConfig &operator=(DatabaseConfig &&) = delete;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_CONFIG_DATABASE_CONFIG_H
