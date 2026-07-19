/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "service_manager/database_service.h"

#include "common/config.h"
#include "common/utils/file.h"
#include "common/utils/password.h"
#include "common/utils/path.h"
#include "common/utils/yaml_resolve.h"
#include "qifeng_framework/common/logger.h"
#include "service_manager/file_manager.h"
#include "service_tool/tool_mariadb.h"
#include "service_tool/tools_def.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

namespace qifeng::scm {

    DatabaseService::DatabaseService(const ServiceContext &ctx) : mCtx(ctx) {
    }

    DatabaseService::~DatabaseService() = default;

    ResultMsg DatabaseService::InitServiceDatabase(const std::string &serviceName) {
        auto svc = mCtx.configLoader->GetServiceByName(serviceName);
        if (svc == nullptr) {
            return MakeError("Service not found: " + serviceName);
        }

        if (svc->dbInfo.dbType != DatabaseType::NONE && !svc->dbInfo.sqlDir.empty()) {
            SLOG_INFO << "Creating database user for service: " << serviceName;
            std::string dbUserName = serviceName;
            std::string dbPassword = utils::GenerateRandomPassword(12);
            SLOG_DEBUG << "start create " << serviceName << " db user password file ";
            ResultMsg res = CreateDbUserPassword(*svc, dbUserName, dbPassword);
            if (!res.IsDefaultSuccess()) {
                SLOG_WARN << "Write db user password error, do clear  ";
                ClearDbUserPasswordFile(*svc);
                return res;
            }
            res = CreateDatabaseUser(svc->dbInfo.dbType, dbUserName, dbPassword);
            if (!res.IsDefaultSuccess()) {
                SLOG_WARN << "Create database user error, do clear  ";
                ClearDbUserPasswordFile(*svc);
                return res;
            }

            SLOG_INFO << "Executing database init scripts for service: " << serviceName;
            // sqlDir 是当前服务的相对路径，基于当前服务目录解析为绝对路径
            std::string absSqlDir = utils::GetAbsolutePath(utils::JoinPath(svc->currentServiceDir, svc->dbInfo.sqlDir));
            res = ExecuteDbInitScripts(svc->dbInfo.dbType, absSqlDir, dbUserName, dbPassword);
            if (!res.IsDefaultSuccess()) {
                // 执行脚本失败，删除用户
                SLOG_WARN << "Failed to execute database init scripts for service: " << serviceName
                          << ", deleting user: " << dbUserName;
                DeleteDatabaseUser(svc->dbInfo.dbType, dbUserName, dbPassword);
                ClearDbUserPasswordFile(*svc);
            }
            return res;
        }
        return MakeSuccess();
    }

    std::string DatabaseService::GetDependentDatabaseServiceName(const std::string &serviceName) {
        auto svc = mCtx.configLoader->GetServiceByName(serviceName);
        if (svc == nullptr) {
            return "";
        }
        std::string dbServiceName;
        for (auto &[name, _] : svc->dependencies) {
            if (tool::IsMariadbService(name)) {
                dbServiceName = name;
                break;
            }
        }
        return dbServiceName;
    }

    ResultMsg DatabaseService::CreateDbUserPassword(const ServiceDefinition &svc, const std::string &dbUserName,
                                                    const std::string &dbPassword) {
        std::string actualFilePath = utils::JoinPath(svc.currentServiceDir, svc.dbInfo.outputDir);
        auto res = utils::CreateDirectory(actualFilePath);
        if (!res.IsDefaultSuccess()) {
            return MakeError("Write db user password error when create directory failed: " + actualFilePath);
        }
        std::string dbPassFile = utils::JoinPath(actualFilePath, svc.serviceName);
        std::ofstream ofs(dbPassFile, std::ios::trunc);
        if (!ofs.is_open()) {
            return MakeError("Write db user password error when failed to open file: " + dbPassFile);
        }
        ofs << dbUserName << std::endl << dbPassword << std::endl;
        return MakeSuccess();
    }

    ResultMsg DatabaseService::ClearDbUserPasswordFile(const ServiceDefinition &svc) {
        std::string actualFilePath = utils::JoinPath(svc.currentServiceDir, svc.dbInfo.outputDir);
        std::string dbPassFile = utils::JoinPath(actualFilePath, svc.serviceName);
        auto res = utils::RemoveFile(dbPassFile);
        if (!res.IsDefaultSuccess()) {
            return MakeError("Clear db user password file error when remove file failed: " + dbPassFile);
        }
        return MakeSuccess();
    }

    ResultMsg DatabaseService::CreateDatabaseUser(const DatabaseType &dbType, const std::string &username,
                                                  const std::string &password) {
        if (dbType == DatabaseType::MYSQL) {
            tool::MariadbDef dbDef;
            tool::Mariadb mariadb(tool::MariadbDef {});
            return mariadb.CreateUser(username, password);
        }
        return MakeError("Unsupported database service ");
    }

    ResultMsg DatabaseService::DeleteDatabaseUser(const DatabaseType &dbType, const std::string &username,
                                                  const std::string &password) {
        if (dbType == DatabaseType::MYSQL) {
            tool::Mariadb mariadb(tool::MariadbDef {});
            return mariadb.DeleteUserAndDatabase(username, password);
        }
        return MakeError("Unsupported database service ");
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    ResultMsg DatabaseService::ExecuteDbInitScripts(const DatabaseType &dbType, const std::string &sqlDir,
                                                    const std::string &dbUserName, const std::string &dbPassword) {
        if (dbType == DatabaseType::MYSQL) {
            tool::Mariadb mariadb(tool::MariadbDef {});
            // 获得sqlDir下全部以.sql结尾的脚本绝对路径
            std::vector<std::string> sqlFiles;
            ResultMsg res = utils::GetAllFilesInDir(sqlFiles, sqlDir, ".sql");
            if (!res.IsDefaultSuccess()) {
                return MakeError("Get all sql files error: " + res.msg);
            }
            std::sort(sqlFiles.begin(), sqlFiles.end());
            for (const auto &sqlFile : sqlFiles) {
                SLOG_INFO << "Executing SQL file: " << sqlFile;
                res = mariadb.ExecuteSqlFileByUser(dbUserName, dbPassword, sqlFile);
                if (!res.IsDefaultSuccess()) {
                    return MakeError("Execute SQL file: " + sqlFile + " error: " + res.msg);
                }
            }
        } else {
            return MakeError("Unsupported database service");
        }
        return MakeSuccess();
    }

    ResultMsg DatabaseService::ClearDatabaseData(const ServiceDefinition &svc) {
        // 从密码文件读取用户密码，用于以用户身份发现所有可访问数据库
        std::string password;
        std::string dbPassFile =
            utils::JoinPath(utils::JoinPath(svc.currentServiceDir, svc.dbInfo.outputDir), svc.serviceName);
        std::ifstream ifs(dbPassFile);
        if (ifs.is_open()) {
            std::string userName;
            std::getline(ifs, userName);  // 第一行：用户名
            std::getline(ifs, password);  // 第二行：密码
            ifs.close();
        } else {
            SLOG_WARN << "Cannot read db user password file: " << dbPassFile
                      << ", will use mysql.db query only to find databases";
        }
        return DeleteDatabaseUser(svc.dbInfo.dbType, svc.serviceName, password);
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg DatabaseService::BackupServiceDatabase(const std::string &serviceName) {
        auto* svc = mCtx.configLoader->GetServiceByName(serviceName);
        if (svc == nullptr) {
            return MakeSuccess();
        }
        // 检查依赖是否包含 mariadb/mysql
        bool dependsMariadb = false;
        for (const auto &dep : svc->dependencies) {
            if (tool::IsMariadbService(dep.first)) {
                dependsMariadb = true;
                break;
            }
        }
        if (!dependsMariadb) {
            return MakeSuccess();
        }
        // 检查 service.yaml 是否含 initDB_sql_dir
        std::string yamlPath = mCtx.fileManager->GetServiceConfigPath(serviceName);
        std::string sqlDir = utils::ReadInitSqlDir(yamlPath);
        if (sqlDir.empty()) {
            return MakeSuccess();
        }

        // 准备 db_backup 目录
        std::string backupRoot = mCtx.fileManager->GetCurDirConfig().backupDir;
        std::string dbBackupDir = utils::JoinPath(backupRoot, serviceName, "db_backup");
        auto ret = utils::CreateDirectory(dbBackupDir);
        if (!ret.IsDefaultSuccess()) {
            return MakeError("Failed to create db_backup dir: " + ret.msg);
        }

        // 使用服务名作为 DB 用户名（与 InitServiceDatabase 约定一致）
        tool::Mariadb mariadb(tool::MariadbDef {});
        auto backupResult = mariadb.BackupUserDatabases(serviceName, dbBackupDir);
        if (!backupResult.IsDefaultSuccess()) {
            return MakeError("Failed to backup databases: " + backupResult.msg);
        }
        SLOG_INFO << "Database backup completed for service: " << serviceName << ", databases: " << backupResult.msg;
        return MakeResult(0, dbBackupDir);
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg DatabaseService::ExecuteUpgradeScripts(const std::string &serviceName) {
        auto* svc = mCtx.configLoader->GetServiceByName(serviceName);
        if (svc == nullptr) {
            return MakeError("Service not found: " + serviceName);
        }
        // 读取 service.yaml 的 initDB_sql_dir
        std::string yamlPath = mCtx.fileManager->GetServiceConfigPath(serviceName);
        std::string sqlDir = utils::ReadInitSqlDir(yamlPath);
        if (sqlDir.empty()) {
            return MakeSuccess();
        }

        // 解析 SQL 脚本目录绝对路径
        std::string absSqlDir = utils::GetAbsolutePath(utils::JoinPath(svc->currentServiceDir, sqlDir));
        if (!std::filesystem::exists(absSqlDir)) {
            SLOG_WARN << "initDB_sql_dir does not exist, skip: " << absSqlDir;
            return MakeSuccess();
        }

        // 升级期必须复用安装期创建的服务账号执行 SQL，禁止使用管理账号（root）
        // 原因：1) 服务账号仅对自己前缀的库有权限，避免误改其它库；2) 管理账号风险过大
        // 服务账号约定：用户名 = serviceName，密码文件位于 <outputDir>/<serviceName>
        if (svc->dbInfo.outputDir.empty()) {
            // service.yaml 未配置 db_output_dir，无法定位密码文件
            SLOG_ERROR << "db_output_dir is empty, cannot locate service account credential";
            return MakeError("db_output_dir is empty, cannot locate service account credential");
        }

        std::string passFile = utils::JoinPath(svc->currentServiceDir, svc->dbInfo.outputDir, serviceName);
        if (!std::filesystem::exists(passFile)) {
            // 密码文件不存在（安装期未生成），无法以服务账号身份执行升级 SQL
            SLOG_ERROR << "Service account credential file not found: " << passFile;
            return MakeError("Service account credential file not found: " + passFile);
        }

        std::string dbUser = serviceName;
        std::string dbPassword;
        std::ifstream ifs(passFile);
        if (ifs.is_open()) {
            // 第一行为用户名，第二行为密码（与安装期写入格式一致）
            std::getline(ifs, dbUser);
            std::getline(ifs, dbPassword);
            ifs.close();
        }

        // 用户名或密码为空视为凭证异常，直接失败（避免误用管理账号）
        if (dbUser.empty() || dbPassword.empty()) {
            SLOG_ERROR << "Service account credential is empty in file: " << passFile << ", user=" << dbUser
                       << ", password=" << (dbPassword.empty() ? "empty" : "non-empty");
            return MakeError("Service account credential is empty in file: " + passFile);
        }

        // 执行 .sql 脚本（按字母序），统一以服务账号身份执行
        std::vector<std::string> sqlFiles;
        auto res = utils::GetAllFilesInDir(sqlFiles, absSqlDir, ".sql");
        if (!res.IsDefaultSuccess()) {
            return MakeError("Get sql files error: " + res.msg);
        }
        std::sort(sqlFiles.begin(), sqlFiles.end());

        tool::Mariadb mariadb(tool::MariadbDef {});
        for (const auto &sqlFile : sqlFiles) {
            SLOG_INFO << "Executing upgrade SQL file: " << sqlFile << " as user: " << dbUser;
            res = mariadb.ExecuteSqlFileByUser(dbUser, dbPassword, sqlFile);
            if (!res.IsDefaultSuccess()) {
                return MakeError("Execute SQL file " + sqlFile + " failed: " + res.msg);
            }
        }
        SLOG_INFO << "Database upgrade scripts executed for service: " << serviceName;
        return MakeSuccess();
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg DatabaseService::RollbackServiceDatabase(const std::string &serviceName) {
        std::string backupRoot = mCtx.fileManager->GetCurDirConfig().backupDir;
        std::string dbBackupDir = utils::JoinPath(backupRoot, serviceName, "db_backup");
        if (!std::filesystem::exists(dbBackupDir)) {
            return MakeSuccess();
        }
        tool::Mariadb mariadb(tool::MariadbDef {});
        auto result = mariadb.RestoreUserDatabases(dbBackupDir);
        if (!result.IsDefaultSuccess()) {
            return MakeError("Failed to rollback databases: " + result.msg);
        }
        SLOG_INFO << "Database rollback completed for service: " << serviceName;
        return MakeSuccess();
    }

}  // namespace qifeng::scm
