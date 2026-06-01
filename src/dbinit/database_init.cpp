/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "dbinit/database_init.h"
#include "dbinit/mysql_backend.h"
#include "dbinit/opengauss_backend.h"

namespace qifeng {
    namespace scm {

        DatabaseInit::DatabaseInit(const DatabaseConfig &config)
            : mConfig(config), mBackend(CreateBackend(mConfig.dbInfo.dbType)) {
        }

        std::unique_ptr<IDatabaseBackend> DatabaseInit::CreateBackend(DatabaseType dbType) {
            switch (dbType) {
                case DatabaseType::MYSQL:
                    return std::make_unique<MySQLBackend>();
                case DatabaseType::OPENGAUSS:
                    return std::make_unique<OpenGaussBackend>();
                default:
                    return nullptr;
            }
        }

        ResultMsg DatabaseInit::Initialize() {
            if (!mBackend) {
                return MakeError("不支持的数据库类型，初始化失败");
            }
            return mBackend->Initialize(mConfig);
        }

        ResultMsg DatabaseInit::Start() {
            if (!mBackend) {
                return MakeError("不支持的数据库类型，启动失败");
            }
            return mBackend->Start(mConfig);
        }

        ResultMsg DatabaseInit::Stop() {
            if (!mBackend) {
                return MakeError("不支持的数据库类型，停止失败");
            }
            return mBackend->Stop(mConfig);
        }

        bool DatabaseInit::IsRunning() const {
            if (!mBackend) {
                return false;
            }
            return mBackend->IsRunning(mConfig);
        }

        ResultMsg DatabaseInit::CreateUser(const std::string &username, const std::string &password) {
            if (!mBackend) {
                return MakeError("不支持的数据库类型，创建用户失败");
            }
            return mBackend->CreateUser(mConfig, username, password);
        }

        ResultMsg DatabaseInit::DeleteUser(const std::string &username) {
            if (!mBackend) {
                return MakeError("不支持的数据库类型，删除用户失败");
            }
            return mBackend->DeleteUser(mConfig, username);
        }

        ResultMsg DatabaseInit::CreateDatabase(const std::string &dbName) {
            if (!mBackend) {
                return MakeError("不支持的数据库类型，创建数据库失败");
            }
            return mBackend->CreateDatabase(mConfig, dbName);
        }

        ResultMsg DatabaseInit::DeleteDatabase(const std::string &dbName) {
            if (!mBackend) {
                return MakeError("不支持的数据库类型，删除数据库失败");
            }
            return mBackend->DeleteDatabase(mConfig, dbName);
        }

        ResultMsg DatabaseInit::CreateTable(const std::string &dbName, const std::string &tableName,
                                            const std::string &columns) {
            if (!mBackend) {
                return MakeError("不支持的数据库类型，创建表失败");
            }
            return mBackend->CreateTable(mConfig, dbName, tableName, columns);
        }

        ResultMsg DatabaseInit::DropTable(const std::string &dbName, const std::string &tableName) {
            if (!mBackend) {
                return MakeError("不支持的数据库类型，删除表失败");
            }
            return mBackend->DropTable(mConfig, dbName, tableName);
        }

        ResultMsg DatabaseInit::ExecuteSQL(const std::string &sql, const std::string &dbName) {
            if (!mBackend) {
                return MakeError("不支持的数据库类型，执行SQL失败");
            }
            return mBackend->ExecuteSQL(mConfig, sql, dbName);
        }

        ResultMsg DatabaseInit::SetDataDir(const std::string &dataDir) {
            if (!mBackend) {
                return MakeError("不支持的数据库类型，设置数据目录失败");
            }
            // SetDataDir 会修改 config_，因此传入非 const 引用
            return mBackend->SetDataDir(mConfig, dataDir);
        }

        ResultMsg DatabaseInit::ExecuteInitScripts() {
            if (!mBackend) {
                return MakeError("不支持的数据库类型，执行初始化脚本失败");
            }
            return mBackend->ExecuteInitScripts(mConfig);
        }

        const DatabaseConfig &DatabaseInit::GetConfig() const {
            return mConfig;
        }

    }  // namespace scm
}  // namespace qifeng
