/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "dbinit/opengauss_backend.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
    // 执行 shell 命令，合并捕获 stdout 和 stderr
    int ExecCommand(const std::string &cmd, std::string &output) {
        std::string fullCmd = cmd + " 2>&1";
        FILE* pipe = popen(fullCmd.c_str(), "r");
        if (!pipe) {
            output = "popen 调用失败";
            return -1;
        }

        std::array<char, 256> buffer {};
        output.clear();
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            output += buffer.data();
        }

        int status = pclose(pipe);
        if (status == -1) {
            return -1;
        }
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        }
        return -1;
    }

    // 通过 TCP 连接探测端口是否可达，timeoutSec 内未连通则返回 false
    bool WaitForPort(int port, int timeoutSec) {
        for (int i = 0; i < timeoutSec; ++i) {
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            struct timeval tv {};
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

            struct sockaddr_in addr {};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(static_cast<uint16_t>(port));
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

            bool connected =
                (connect(sock, static_cast<struct sockaddr*>(static_cast<void*>(&addr)), sizeof(addr)) == 0);
            close(sock);
            if (connected) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        return false;
    }

    // 将内容写入指定文件
    bool WriteFile(const std::string &filePath, const std::string &content) {
        std::ofstream ofs(filePath, std::ios::out | std::ios::trunc);
        if (!ofs.is_open()) {
            return false;
        }
        ofs << content;
        ofs.close();
        return !ofs.fail();
    }

    // SQL 单引号转义：PostgreSQL/OpenGauss 规范，单引号需写两遍
    std::string EscapeSQLString(const std::string &value) {
        std::string result;
        result.reserve(value.size());
        for (char c : value) {
            if (c == '\'') {
                result.push_back('\'');
            }
            result.push_back(c);
        }
        return result;
    }

    // 读取文件全部内容，使用 seekg/tellg/read 避免 GCC istreambuf_iterator 空指针误报
    std::string ReadFile(const std::string &path) {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) {
            return "";
        }
        std::string content;
        ifs.seekg(0, std::ios::end);
        auto sz = ifs.tellg();
        if (sz > 0) {
            content.resize(static_cast<size_t>(sz));
            ifs.seekg(0, std::ios::beg);
            ifs.read(&content[0], static_cast<std::streamsize>(sz));
        }
        return content;
    }

    // 通过临时文件执行 SQL，避免命令行拼接导致的 shell 注入
    qifeng::scm::ResultMsg ExecuteSQLViaTempFile(const std::string &gsqlCmd, const std::string &osUser,
                                                 const std::string &osGroup, const std::string &sql) {
        namespace fs = std::filesystem;
        using qifeng::scm::MakeError;
        using qifeng::scm::MakeSuccess;

        std::string tmpFile = "/tmp/.dbinit_og_" + std::to_string(getpid()) + ".sql";

        if (!WriteFile(tmpFile, sql + "\n")) {
            return MakeError("无法写入临时SQL文件: " + tmpFile);
        }

        // 仅属主可读写，防止其他用户窥探 SQL 内容
        std::error_code ec;
        fs::permissions(tmpFile, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
        if (ec) {
            fs::remove(tmpFile, ec);
            return MakeError("设置临时SQL文件权限失败: " + ec.message());
        }

        // 属主改为 osUser，以便 su 后的 gsql 进程可读取
        std::string chownCmd = "chown " + osUser + ":" + osGroup + " '" + tmpFile + "'";
        std::string output;
        if (ExecCommand(chownCmd, output) != 0) {
            fs::remove(tmpFile, ec);
            return MakeError("chown 临时SQL文件失败: " + output);
        }

        // 以 osUser 身份执行 gsql
        std::string execCmd = gsqlCmd + " -f '" + tmpFile + "'";
        std::string suCmd = "su - " + osUser + " -c \"" + execCmd + "\"";
        int ret = ExecCommand(suCmd, output);

        // 无论成功与否，均清理临时文件
        fs::remove(tmpFile, ec);

        if (ret != 0) {
            return MakeError("执行SQL失败: " + output);
        }
        return MakeSuccess();
    }

    // 创建数据目录并设置权限 700 和属主
    qifeng::scm::ResultMsg SetupDataDirectory(const std::string &dataDir, const std::string &osUser,
                                              const std::string &osGroup) {
        namespace fs = std::filesystem;
        using qifeng::scm::MakeError;

        std::error_code ec;
        if (!fs::exists(dataDir)) {
            fs::create_directories(dataDir, ec);
            if (ec) {
                return MakeError("创建数据目录失败: " + ec.message());
            }
        }

        // OpenGauss 强制要求数据目录权限为 700
        fs::permissions(dataDir, fs::perms::owner_all, fs::perm_options::replace, ec);
        if (ec) {
            return MakeError("设置数据目录权限失败: " + ec.message());
        }

        // 数据目录属主改为 osUser，gs_initdb 需要以该用户身份运行
        std::string output;
        std::string chownCmd = "chown " + osUser + ":" + osGroup + " '" + dataDir + "'";
        if (ExecCommand(chownCmd, output) != 0) {
            return MakeError("chown 数据目录失败: " + output);
        }

        return qifeng::scm::MakeSuccess();
    }

    // 创建 gs_initdb 所需的密码文件，写入后 chown 为 osUser
    // 返回密码文件路径；失败时返回空字符串
    std::string CreatePwdFile(const std::string &dataDir, const std::string &password, const std::string &osUser,
                              const std::string &osGroup) {
        std::string pwdFile = dataDir + "/.init_pwd";
        if (!WriteFile(pwdFile, password + "\n")) {
            return {};
        }

        std::string output;
        std::string chownCmd = "chown " + osUser + ":" + osGroup + " '" + pwdFile + "'";
        if (ExecCommand(chownCmd, output) != 0) {
            std::error_code ec;
            std::filesystem::remove(pwdFile, ec);
            return {};
        }

        return pwdFile;
    }

    // 校验 Initialize 所需的配置参数
    qifeng::scm::ResultMsg ValidateInitConfig(const qifeng::scm::DatabaseConfig &config) {
        using qifeng::scm::MakeError;
        if (config.dataDir.empty()) {
            return MakeError("数据目录不能为空");
        }
        if (config.binDir.empty()) {
            return MakeError("二进制目录不能为空");
        }
        if (config.osUser.empty()) {
            return MakeError("操作系统用户不能为空");
        }
        if (config.adminPwd.empty()) {
            return MakeError("管理员密码不能为空");
        }
        return qifeng::scm::MakeSuccess();
    }

    // 以 osUser 身份执行 gs_initdb，完成后自动清理密码文件
    qifeng::scm::ResultMsg RunInitdb(const std::string &binDir, const std::string &dataDir, const std::string &osUser,
                                     const std::string &pwdFile) {
        using qifeng::scm::MakeError;
        namespace fs = std::filesystem;

        std::string initdbCmd =
            binDir + "/gs_initdb -D '" + dataDir + "' --locale=en_US.UTF-8 --pwfile='" + pwdFile + "'";
        std::string suInitCmd = "su - " + osUser + " -c \"" + initdbCmd + "\"";
        std::string output;
        int ret = ExecCommand(suInitCmd, output);

        // 无论初始化成功与否，均删除密码文件
        std::error_code ec;
        fs::remove(pwdFile, ec);

        if (ret != 0) {
            return MakeError("gs_initdb 执行失败: " + output);
        }
        return qifeng::scm::MakeSuccess();
    }
}  // namespace

namespace qifeng {
    namespace scm {

        int OpenGaussBackend::GetEffectivePort(const DatabaseConfig &config) {
            return (config.port != 0) ? config.port : GetDefaultPort();
        }

        std::string OpenGaussBackend::GetGsqlClientCmd(const DatabaseConfig &config, const std::string &dbName) {
            int port = GetEffectivePort(config);
            return config.binDir + "/gsql -d '" + dbName + "' -p " + std::to_string(port);
        }

        ResultMsg OpenGaussBackend::Initialize(const DatabaseConfig &config) {
            namespace fs = std::filesystem;

            auto validateResult = ValidateInitConfig(config);
            if (!validateResult.IsDefalutSuccess()) {
                return validateResult;
            }

            // 通过 PG_VERSION 判断是否已完成初始化
            fs::path pgVersionPath = fs::path(config.dataDir) / "PG_VERSION";
            if (fs::exists(pgVersionPath)) {
                return MakeWarning("数据目录已初始化，跳过: " + config.dataDir);
            }

            // 创建数据目录并设置权限与属主
            auto dirResult = SetupDataDirectory(config.dataDir, config.osUser, config.osGroup);
            if (!dirResult.IsDefalutSuccess()) {
                return dirResult;
            }

            // 创建密码文件供 gs_initdb --pwfile 使用
            std::string pwdFile = CreatePwdFile(config.dataDir, config.adminPwd, config.osUser, config.osGroup);
            if (pwdFile.empty()) {
                return MakeError("无法创建密码文件: " + config.dataDir + "/.init_pwd");
            }

            // 以 osUser 身份执行 gs_initdb（内部会清理密码文件）
            auto initdbResult = RunInitdb(config.binDir, config.dataDir, config.osUser, pwdFile);
            if (!initdbResult.IsDefalutSuccess()) {
                return initdbResult;
            }

            // 生成 postgresql.conf 配置
            auto configResult = GenerateConfig(config);
            if (!configResult.IsDefalutSuccess()) {
                return MakeError("生成配置文件失败: " + configResult.msg);
            }

            return MakeSuccess();
        }

        ResultMsg OpenGaussBackend::Start(const DatabaseConfig &config) {
            if (config.dataDir.empty()) {
                return MakeError("数据目录不能为空");
            }

            std::string startCmd = config.binDir + "/gs_ctl start -D '" + config.dataDir + "'";
            std::string suCmd = "su - " + config.osUser + " -c \"" + startCmd + "\"";
            std::string output;
            int ret = ExecCommand(suCmd, output);
            if (ret != 0) {
                return MakeError("启动 OpenGauss 失败: " + output);
            }

            // 等待端口就绪
            int port = GetEffectivePort(config);
            if (!WaitForPort(port, 30)) {
                return MakeError("OpenGauss 启动超时，端口 " + std::to_string(port) + " 未就绪");
            }

            return MakeSuccess();
        }

        ResultMsg OpenGaussBackend::Stop(const DatabaseConfig &config) {
            if (config.dataDir.empty()) {
                return MakeError("数据目录不能为空");
            }

            std::string stopCmd = config.binDir + "/gs_ctl stop -D '" + config.dataDir + "'";
            std::string suCmd = "su - " + config.osUser + " -c \"" + stopCmd + "\"";
            std::string output;
            int ret = ExecCommand(suCmd, output);
            if (ret != 0) {
                return MakeError("停止 OpenGauss 失败: " + output);
            }

            return MakeSuccess();
        }

        bool OpenGaussBackend::IsRunning(const DatabaseConfig &config) const {
            int port = GetEffectivePort(config);
            return WaitForPort(port, 1);
        }

        ResultMsg OpenGaussBackend::ExecuteSQL(const DatabaseConfig &config, const std::string &sql,
                                               const std::string &dbName) {
            if (sql.empty()) {
                return MakeError("SQL 语句不能为空");
            }

            // 未指定数据库时使用 postgres 默认库
            std::string effectiveDbName = dbName.empty() ? "postgres" : dbName;
            std::string gsqlCmd = GetGsqlClientCmd(config, effectiveDbName);

            return ExecuteSQLViaTempFile(gsqlCmd, config.osUser, config.osGroup, sql);
        }

        ResultMsg OpenGaussBackend::CreateUser(const DatabaseConfig &config, const std::string &username,
                                               const std::string &password) {
            if (username.empty()) {
                return MakeError("用户名不能为空");
            }

            std::string sql =
                "CREATE USER " + username + " WITH ENCRYPTED PASSWORD '" + EscapeSQLString(password) + "';";
            return ExecuteSQL(config, sql, "postgres");
        }

        ResultMsg OpenGaussBackend::DeleteUser(const DatabaseConfig &config, const std::string &username) {
            if (username.empty()) {
                return MakeError("用户名不能为空");
            }

            std::string sql = "DROP USER IF EXISTS " + username + ";";
            return ExecuteSQL(config, sql, "postgres");
        }

        ResultMsg OpenGaussBackend::CreateDatabase(const DatabaseConfig &config, const std::string &dbName) {
            if (dbName.empty()) {
                return MakeError("数据库名不能为空");
            }

            std::string sql = "CREATE DATABASE " + dbName + ";";
            return ExecuteSQL(config, sql, "postgres");
        }

        ResultMsg OpenGaussBackend::DeleteDatabase(const DatabaseConfig &config, const std::string &dbName) {
            if (dbName.empty()) {
                return MakeError("数据库名不能为空");
            }

            std::string sql = "DROP DATABASE IF EXISTS " + dbName + ";";
            return ExecuteSQL(config, sql, "postgres");
        }

        ResultMsg OpenGaussBackend::CreateTable(const DatabaseConfig &config, const std::string &dbName,
                                                const std::string &tableName, const std::string &columns) {
            if (dbName.empty()) {
                return MakeError("数据库名不能为空");
            }
            if (tableName.empty()) {
                return MakeError("表名不能为空");
            }
            if (columns.empty()) {
                return MakeError("列定义不能为空");
            }

            std::string sql = "CREATE TABLE " + tableName + " (" + columns + ");";
            return ExecuteSQL(config, sql, dbName);
        }

        ResultMsg OpenGaussBackend::DropTable(const DatabaseConfig &config, const std::string &dbName,
                                              const std::string &tableName) {
            if (dbName.empty()) {
                return MakeError("数据库名不能为空");
            }
            if (tableName.empty()) {
                return MakeError("表名不能为空");
            }

            std::string sql = "DROP TABLE IF EXISTS " + tableName + ";";
            return ExecuteSQL(config, sql, dbName);
        }

        ResultMsg OpenGaussBackend::SetDataDir(DatabaseConfig &config, const std::string &dataDir) {
            namespace fs = std::filesystem;

            if (dataDir.empty()) {
                return MakeError("数据目录路径不能为空");
            }

            // 必须为绝对路径
            if (dataDir[0] != '/') {
                return MakeError("数据目录必须为绝对路径: " + dataDir);
            }

            // 若目标路径已初始化则拒绝，避免覆盖已有数据
            fs::path pgVersionPath = fs::path(dataDir) / "PG_VERSION";
            if (fs::exists(pgVersionPath)) {
                return MakeError("目标目录已包含已初始化的数据库: " + dataDir);
            }

            config.dataDir = dataDir;

            // 确保目录存在
            std::error_code ec;
            if (!fs::exists(dataDir)) {
                fs::create_directories(dataDir, ec);
                if (ec) {
                    return MakeError("创建数据目录失败: " + ec.message());
                }
            }

            // OpenGauss 强制要求数据目录权限为 700
            fs::permissions(dataDir, fs::perms::owner_all, fs::perm_options::replace, ec);
            if (ec) {
                return MakeError("设置数据目录权限失败: " + ec.message());
            }

            return MakeSuccess();
        }

        ResultMsg OpenGaussBackend::GenerateConfig(const DatabaseConfig &config) const {
            namespace fs = std::filesystem;

            std::string confPath = config.dataDir + "/postgresql.conf";
            if (!fs::exists(confPath)) {
                return MakeError("postgresql.conf 不存在: " + confPath);
            }

            int port = GetEffectivePort(config);

            // 将需要追加的配置写入临时文件，再以 osUser 身份追加到 postgresql.conf
            // PostgreSQL/OpenGauss 中后出现的同名配置项覆盖先前的，因此追加方式安全
            std::string tmpConf = "/tmp/.dbinit_og_conf_" + std::to_string(getpid());
            std::string content = "port = " + std::to_string(port) + "\nlisten_addresses = '*'\n";
            if (!WriteFile(tmpConf, content)) {
                return MakeError("无法写入临时配置文件: " + tmpConf);
            }

            std::string output;
            std::string chownCmd = "chown " + config.osUser + ":" + config.osGroup + " '" + tmpConf + "'";
            if (ExecCommand(chownCmd, output) != 0) {
                std::error_code ec;
                fs::remove(tmpConf, ec);
                return MakeError("chown 临时配置文件失败: " + output);
            }

            std::string appendCmd = "su - " + config.osUser + " -c \"cat '" + tmpConf + "' >> '" + confPath + "'\"";
            int ret = ExecCommand(appendCmd, output);

            std::error_code ec;
            fs::remove(tmpConf, ec);

            if (ret != 0) {
                return MakeError("追加配置到 postgresql.conf 失败: " + output);
            }

            return MakeSuccess();
        }

        // NOLINTBEGIN(readability-function-size, readability-function-cognitive-complexity)
        // 函数大小和复杂度超过阈值，但符合业务逻辑
        ResultMsg OpenGaussBackend::ExecuteInitScripts(const DatabaseConfig &config) {
            namespace fs = std::filesystem;

            // sqlDir 为空则跳过脚本执行
            if (config.dbInfo.sqlDir.empty()) {
                return MakeSuccess();
            }

            // 解析 sqlDir：相对路径基于 binDir 解析为绝对路径
            fs::path sqlDirPath(config.dbInfo.sqlDir);
            if (!sqlDirPath.is_absolute()) {
                sqlDirPath = fs::path(config.binDir) / sqlDirPath;
            }

            if (!fs::exists(sqlDirPath)) {
                return MakeWarning("sqlDir 目录不存在，跳过初始化脚本: " + sqlDirPath.string());
            }

            // 扫描 sqlDir 下所有 .sql 文件，按文件名排序
            std::vector<fs::path> sqlFiles;
            for (const auto &entry : fs::directory_iterator(sqlDirPath)) {
                if (entry.is_regular_file() && entry.path().extension() == ".sql") {
                    sqlFiles.push_back(entry.path());
                }
            }

            if (sqlFiles.empty()) {
                return MakeWarning("sqlDir 下未找到 .sql 文件: " + sqlDirPath.string());
            }

            std::sort(sqlFiles.begin(), sqlFiles.end());

            // 逐个执行 .sql 文件
            for (const auto &sqlFile : sqlFiles) {
                // 使用 ReadFile 读取文件内容，避免 GCC 15.2 istreambuf_iterator 空指针误报
                std::string content = ReadFile(sqlFile.string());
                if (content.empty()) {
                    // 区分"文件读取失败"和"文件内容为空"
                    std::error_code ec;
                    if (!fs::exists(sqlFile, ec) || ec) {
                        return MakeError("无法读取 SQL 脚本: " + sqlFile.string());
                    }
                    // 文件存在但内容为空，跳过
                    continue;
                }

                // OpenGauss 默认连接 postgres 数据库执行脚本
                auto result = ExecuteSQL(config, content, "postgres");
                if (!result.IsDefalutSuccess()) {
                    return MakeError("执行脚本失败 " + sqlFile.filename().string() + ": " + result.msg);
                }
            }

            return MakeSuccess();
        }
        // NOLINTEND(readability-function-size, readability-function-cognitive-complexity)

    }  // namespace scm
}  // namespace qifeng
