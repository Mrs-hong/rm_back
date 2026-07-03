/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "service_tool/tool_mariadb.h"

#include "common/types.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
    // 执行 shell 命令并捕获输出，合并 stderr 到 stdout
    int ExecCommand(const std::string &cmd, std::string &output) {
        std::string fullCmd = cmd + " 2>&1";
        FILE* pipe = popen(fullCmd.c_str(), "r");
        if (pipe == nullptr) {
            output = "popen failed";
            return -1;
        }

        std::array<char, 4096> buffer {};
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            output += buffer.data();
        }

        int status = pclose(pipe);
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        }
        return -1;
    }

    // 对字符串进行 shell 单引号转义：用单引号包裹，内部单引号替换为 '\''
    std::string EscapeShellSingleQuote(const std::string &s) {
        std::string result = "'";
        for (char c : s) {
            if (c == '\'') {
                result += "'\\''";
            } else {
                result += c;
            }
        }
        result += "'";
        return result;
    }

    // SQL 字符串转义：单引号写两遍
    std::string EscapeSqlString(const std::string &s) {
        std::string result;
        result.reserve(s.size());
        for (char c : s) {
            if (c == '\'') {
                result.push_back('\'');
            }
            result.push_back(c);
        }
        return result;
    }

    // 检测系统 mysql/mariadb 客户端命令路径
    std::string DetectMysqlClient() {
        if (fs::exists("/usr/bin/mariadb")) {
            return "/usr/bin/mariadb";
        }
        if (fs::exists("/usr/bin/mysql")) {
            return "/usr/bin/mysql";
        }
        return "mysql";
    }

    // 构建管理员连接命令
    std::string BuildAdminClientCmd(const qifeng::scm::tool::MariadbDef &def) {
        std::ostringstream oss;
        oss << DetectMysqlClient();
        oss << " -h " << def.host;
        oss << " -P " << def.port;
        oss << " -u " << def.adminUser;
        if (!def.adminPassword.empty()) {
            oss << " -p" << EscapeShellSingleQuote(def.adminPassword);
        }
        return oss.str();
    }

    // 通过临时文件执行 SQL，防止 shell 注入
    qifeng::scm::ResultMsg ExecuteSQLViaTempFile(const std::string &clientCmd, const std::string &sql) {
        std::string tmpFile = "/tmp/.scm_mariadb_" + std::to_string(getpid()) + ".sql";

        std::ofstream ofs(tmpFile);
        if (!ofs) {
            return qifeng::scm::MakeError("Failed to write temp SQL file: " + tmpFile);
        }
        ofs << sql;
        ofs.close();
        chmod(tmpFile.c_str(), S_IRUSR | S_IWUSR);

        std::string fullCmd = clientCmd + " < " + tmpFile;
        std::string output;
        int ret = ExecCommand(fullCmd, output);
        fs::remove(tmpFile);

        if (ret != 0) {
            return qifeng::scm::MakeError("SQL execution failed: " + output);
        }
        return qifeng::scm::MakeSuccess();
    }
}  // namespace

namespace qifeng::scm::tool {

    Mariadb::Mariadb(const MariadbDef &def) : mDef(def) {
    }

    ResultMsg Mariadb::CreateUser(const std::string &user, const std::string &password) {
        if (user.empty()) {
            return MakeError("Username is empty");
        }

        // SQL 转义用户名和密码中的单引号
        std::string escapedUser = EscapeSqlString(user);
        std::string escapedPwd = EscapeSqlString(password);

        // 创建用户：localhost 和 127.0.0.1 连接权限
        std::string sql = "CREATE USER IF NOT EXISTS '" + escapedUser + "'@'localhost' IDENTIFIED BY '" + escapedPwd +
                          "';\n"
                          "CREATE USER IF NOT EXISTS '" +
                          escapedUser + "'@'127.0.0.1' IDENTIFIED BY '" + escapedPwd + "';\n";

        // 授予创建数据库权限
        sql += "GRANT CREATE ON *.* TO '" + escapedUser + "'@'localhost';\n";
        sql += "GRANT CREATE ON *.* TO '" + escapedUser + "'@'127.0.0.1';\n";

        // 授予用户在自己创建的数据库上所有权限（用户名前缀匹配）
        sql += "GRANT ALL PRIVILEGES ON `" + user + "_%`.* TO '" + escapedUser + "'@'localhost';\n";
        sql += "GRANT ALL PRIVILEGES ON `" + user + "_%`.* TO '" + escapedUser + "'@'127.0.0.1';\n";

        sql += "FLUSH PRIVILEGES;\n";

        return ExecuteSQLViaTempFile(BuildAdminClientCmd(mDef), sql);
    }

    // NOLINTNEXTLINE: 17 function exceeds recommended size/complexity thresholds
    ResultMsg Mariadb::DeleteUserAndDatabase(const std::string &user) {
        if (user.empty()) {
            return MakeError("Username is empty");
        }

        // 查询用户拥有的数据库（通过 mysql.db 中 Drop_priv 近似筛选）
        std::string escapedUser = EscapeSqlString(user);
        std::string querySql =
            "SELECT DISTINCT Db FROM mysql.db WHERE User='" + escapedUser +
            "' AND Drop_priv='Y' AND Db NOT IN ('mysql','information_schema','performance_schema','sys');";

        std::string clientCmd = BuildAdminClientCmd(mDef) + " -B -N mysql";

        // 通过临时文件执行查询 SQL
        std::string tmpFile = "/tmp/.scm_mariadb_" + std::to_string(getpid()) + "_query.sql";
        std::ofstream ofs(tmpFile);
        if (!ofs) {
            return MakeError("Failed to write temp SQL file: " + tmpFile);
        }
        ofs << querySql;
        ofs.close();
        chmod(tmpFile.c_str(), S_IRUSR | S_IWUSR);

        std::string fullCmd = clientCmd + " < " + tmpFile;
        std::string output;
        int ret = ExecCommand(fullCmd, output);
        fs::remove(tmpFile);

        if (ret != 0) {
            return MakeError("Failed to query user databases: " + output);
        }

        // 删除查询到的数据库
        if (!output.empty()) {
            std::istringstream iss(output);
            std::string dbName;
            while (std::getline(iss, dbName)) {
                if (!dbName.empty() && dbName.back() == '\r') {
                    dbName.pop_back();
                }
                // 跳过空值、通配符库名、警告信息行
                if (dbName.empty() || dbName.find('%') != std::string::npos || dbName.find(':') != std::string::npos) {
                    continue;
                }
                std::string dropSql = "DROP DATABASE IF EXISTS `" + dbName + "`;\n";
                auto dropResult = ExecuteSQLViaTempFile(BuildAdminClientCmd(mDef), dropSql);
                if (!dropResult.IsDefalutSuccess()) {
                    return MakeError("Failed to delete database '" + dbName + "': " + dropResult.msg);
                }
            }
        }

        // 删除用户（涵盖 localhost、127.0.0.1、% 三种 host）
        std::string sql = "DROP USER IF EXISTS '" + escapedUser +
                          "'@'localhost';\n"
                          "DROP USER IF EXISTS '" +
                          escapedUser +
                          "'@'127.0.0.1';\n"
                          "DROP USER IF EXISTS '" +
                          escapedUser +
                          "'@'%';\n"
                          "FLUSH PRIVILEGES;\n";

        return ExecuteSQLViaTempFile(BuildAdminClientCmd(mDef), sql);
    }

    ResultMsg Mariadb::ExecuteSqlFile(const std::string &sqlFile) {
        if (sqlFile.empty()) {
            return MakeError("SQL file path is empty");
        }
        if (!fs::exists(sqlFile)) {
            return MakeError("SQL file does not exist: " + sqlFile);
        }

        std::string clientCmd = BuildAdminClientCmd(mDef);
        std::string fullCmd = clientCmd + " < " + EscapeShellSingleQuote(sqlFile);
        std::string output;
        int ret = ExecCommand(fullCmd, output);

        if (ret != 0) {
            return MakeError("SQL file execution failed: " + output);
        }
        return MakeSuccess();
    }

    ResultMsg Mariadb::ExecuteSqlFileByUser(const std::string &user, const std::string &password,
                                            const std::string &sqlFilePath) {
        if (user.empty()) {
            return MakeError("Username is empty");
        }
        if (sqlFilePath.empty()) {
            return MakeError("SQL file path is empty");
        }
        if (!fs::exists(sqlFilePath)) {
            return MakeError("SQL file does not exist: " + sqlFilePath);
        }

        std::ostringstream oss;
        oss << DetectMysqlClient();
        oss << " -h " << mDef.host;
        oss << " -P " << mDef.port;
        oss << " -u " << user;
        if (!password.empty()) {
            oss << " -p" << EscapeShellSingleQuote(password);
        }

        std::string fullCmd = oss.str() + " < " + EscapeShellSingleQuote(sqlFilePath);
        std::string output;
        int ret = ExecCommand(fullCmd, output);

        if (ret != 0) {
            return MakeError("SQL file execution failed: " + output);
        }
        return MakeSuccess();
    }

    ResultMsg Mariadb::GetVersion() {
        std::string cmd = DetectMysqlClient() + " --version";
        std::string output;
        int ret = ExecCommand(cmd, output);

        if (ret != 0) {
            return MakeError("Failed to get MariaDB version: " + output);
        }

        // 去除末尾换行符
        while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
            output.pop_back();
        }

        return {0, output};
    }

}  // namespace qifeng::scm::tool
