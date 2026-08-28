/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_KB_INCLUDE_DAO_DB_POOL_H
#define QIFENG_KB_INCLUDE_DAO_DB_POOL_H

#include <cstddef>
#include <string>

#include "common/logger.h"
#include "soci/soci.h"

struct DBPoolConfig {
    std::string dbType;
    std::string server;
    std::string port;
    std::string db;
    std::string user;
    std::string password;
    size_t poolSize = 0;
};

class DBPool {
public:
    static DBPool& GetInstance() {
        static DBPool Instance;
        return Instance;
    }

    soci::session Get() {
        soci::session sql(*mPool);

        if (!ValidateConnection(sql)) {
            SLOG_WARN << "Database connection is invalid, attempting to reconnect...";
            try {
                sql.reconnect();
                SLOG_DEBUG << "reconnect successfully";
            } catch (const soci::soci_error& e) {
                SLOG_ERROR << "Failed to reconnect to the database: " << e.what();
            }
        }
        return sql;
    }
    // 初始化连接池
    bool Initialize(const DBPoolConfig& config = DBPoolConfig {});

    bool ValidateConnection(soci::session& sql) {
        try {
            int result = 0;
            sql << "SELECT 1", soci::into(result);
            return result == 1;
        } catch (const soci::soci_error& e) {
            return false;
        }
    }

private:
    DBPool() = default;

private:
    std::unique_ptr<soci::connection_pool> mPool;
    std::string mDBPath;
    size_t mPoolSize = 0;
};

#endif  // QIFENG_KB_INCLUDE_DAO_DB_POOL_H