/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#include "common/config_manager.h"
#include "common/logger.h"
#include "dao/db_pool.h"

// 初始化连接池
bool DBPool::Initialize(const DBPoolConfig& config) {
    try {
        if (mPool) {
            FLOG_WARN("DBPool is already initialized");
            return false;
        }

        // 读取配置
        std::string server =
            config.server.empty() ? ConfigManager::GetInstance().GetString("database", "mysql_server") : config.server;
        std::string port =
            config.port.empty() ? ConfigManager::GetInstance().GetString("database", "mysql_port") : config.port;
        std::string db = config.db.empty() ? ConfigManager::GetInstance().GetString("database", "mysql_db") : config.db;
        std::string user =
            config.user.empty() ? ConfigManager::GetInstance().GetString("database", "mysql_user") : config.user;
        std::string password = config.password.empty()
                                   ? ConfigManager::GetInstance().GetString("database", "mysql_password")
                                   : config.password;
        std::string dbType =
            config.dbType.empty() ? ConfigManager::GetInstance().GetString("database", "db_type") : config.dbType;
        mPoolSize = config.poolSize == 0
                        ? static_cast<size_t>(ConfigManager::GetInstance().GetInt("database", "mysql_pool_size"))
                        : config.poolSize;
        mDBPath = "host=" + server + " port=" + port + " dbname=" + db + " user=" + user + " password=" + password;

        // MySQL 连接设置 utf8mb4 编码
        if (dbType == "mysql") {
            mDBPath += " charset=utf8mb4";
        }

        FLOG_INFO("Database type: " + dbType);
        FLOG_INFO("Database server: " + server + ":" + port);
        FLOG_INFO("Database name: " + db);
        FLOG_INFO("Database user: " + user);
        // 创建 pool
        mPool = std::make_unique<soci::connection_pool>(mPoolSize);

        for (size_t i = 0; i < mPoolSize; ++i) {
            soci::session& sql = mPool->at(i);
            sql.open(dbType, mDBPath);
        }

        FLOG_INFO("DBPool initialized with " + std::to_string(mPoolSize) + " connections");
        return true;

    } catch (const std::exception& e) {
        FLOG_ERROR("Failed to Initialize DBPool: " + std::string(e.what()));
        return false;
    }
}