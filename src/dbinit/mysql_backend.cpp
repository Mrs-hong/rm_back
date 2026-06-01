/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "dbinit/mysql_backend.h"

#include "common/scmd_types.h"
#include "common/utils.h"

#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <system_error>
#include <thread>
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

    // 尝试 TCP 连接到 127.0.0.1:port，成功返回 true
    // POSIX socket API 要求 sockaddr_in* 转 sockaddr*，此处不可避免
    bool TcpConnect(int port) {
        int sock = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);  // NOLINT(android-cloexec-socket)
        if (sock < 0) {
            return false;
        }

        struct timeval tv {};
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        struct sockaddr_in addr {};  // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        // NOLINT: POSIX connect() 要求 sockaddr_in* 转 sockaddr*，属于标准类型双关
        bool connected = (connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) >=
                          0);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        close(sock);

        return connected;
    }

    // 通过 TCP 连接探测端口是否可达，用于判断数据库是否就绪
    bool WaitForPort(int port, int timeoutSec) {
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(timeoutSec)) {
            if (TcpConnect(port)) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        return false;
    }

    // 将内容写入指定文件
    bool WriteFile(const std::string &path, const std::string &content) {
        std::ofstream ofs(path);
        if (!ofs) {
            return false;
        }
        ofs << content;
        ofs.close();
        return ofs.good();
    }

    // 读取文件全部内容，使用 seekg/tellg/read 避免 GCC istreambuf_iterator 误报
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

    // 通过临时文件执行 SQL，防止 shell 注入
    // 临时文件路径: /tmp/.dbinit_<pid>.sql，权限 600，执行后删除
    qifeng::scm::ResultMsg ExecuteSQLViaTempFile(const std::string &clientCmd, const std::string &sql) {
        std::string tmpFile = "/tmp/.dbinit_" + std::to_string(getpid()) + ".sql";

        if (!WriteFile(tmpFile, sql)) {
            return qifeng::scm::MakeError("Failed to write temp SQL file: " + tmpFile);
        }

        // 限制临时文件权限为仅属主可读写
        chmod(tmpFile.c_str(), S_IRUSR | S_IWUSR);

        std::string fullCmd = clientCmd + " < " + tmpFile;
        std::string output;
        int ret = ExecCommand(fullCmd, output);

        // 用完即删
        fs::remove(tmpFile);

        if (ret != 0) {
            return qifeng::scm::MakeError("SQL execution failed: " + output);
        }
        return qifeng::scm::MakeSuccess();
    }
}  // namespace

namespace qifeng {
    namespace scm {

        // ---- 静态私有方法 ----

        int MySQLBackend::GetEffectivePort(const DatabaseConfig &config) {
            return (config.port != 0) ? config.port : GetDefaultPort();
        }

        std::string MySQLBackend::GetSocketPath(const DatabaseConfig &config) {
            return config.dataDir + "/mysql.sock";
        }

        std::string MySQLBackend::GetMySQLClientCmd(const DatabaseConfig &config, const std::string &dbName) {
            std::ostringstream oss;
            oss << config.binDir << "/bin/mysql" << " -u " << config.adminUser;
            // MariaDB 初始化后 root 密码为空，不需要 -p 参数；MySQL 8.x --initialize-insecure 同理
            if (!config.adminPwd.empty()) {
                oss << " -p" << config.adminPwd;
            }
            oss << " --socket=" << GetSocketPath(config);
            if (!dbName.empty()) {
                oss << " " << dbName;
            }
            return oss.str();
        }

        // ---- 配置文件生成 ----

        ResultMsg MySQLBackend::GenerateConfig(const DatabaseConfig &config) const {
            std::string confPath = config.dataDir + "/my.cnf";
            int port = GetEffectivePort(config);
            std::string socketPath = GetSocketPath(config);

            std::ostringstream oss;
            oss << "[mysqld]\n"
                << "basedir=" << config.binDir << "\n"
                << "datadir=" << config.dataDir << "\n"
                << "port=" << port << "\n"
                << "socket=" << socketPath << "\n"
                << "pid-file=" << config.dataDir << "/mysql.pid\n";

            // 仅当指定 osUser 时才以该用户身份运行，否则以当前用户运行
            if (!config.osUser.empty()) {
                oss << "user=" << config.osUser << "\n";
            }

            // default_authentication_plugin 仅 MySQL 5.7/8.0 支持，MariaDB 不支持
            if (!fs::exists(config.binDir + "/scripts/mysql_install_db") &&
                !fs::exists(config.binDir + "/bin/mysql_install_db")) {
                oss << "default_authentication_plugin=mysql_native_password\n";
            }

            if (!WriteFile(confPath, oss.str())) {
                return MakeError("Failed to write my.cnf: " + confPath);
            }
            return MakeSuccess();
        }

        // ---- 公共接口实现 ----

        // NOLINTBEGIN(readability-function-size, readability-function-cognitive-complexity)
        // 函数大小和复杂度超过阈值，但符合业务逻辑
        ResultMsg MySQLBackend::Initialize(const DatabaseConfig &config) {
            if (config.binDir.empty()) {
                return MakeError("MySQL binDir is empty");
            }
            if (config.dataDir.empty()) {
                return MakeError("MySQL dataDir is empty");
            }

            // 通过 ibdata1 判断是否已初始化，避免重复初始化导致数据丢失
            if (fs::exists(config.dataDir + "/ibdata1")) {
                return MakeWarning("MySQL already initialized (ibdata1 exists)");
            }

            auto dirResult = utils::CreateDirectory(config.dataDir);
            if (!dirResult.IsDefalutSuccess()) {
                return MakeError("Failed to create data directory: " + dirResult.msg);
            }

            // 数据目录需要归属 osUser，否则 mysqld 以 osUser 身份运行时无法写入
            if (!config.osUser.empty()) {
                std::string chownCmd = "chown -R " + config.osUser + ":" + config.osGroup + " " + config.dataDir;
                std::string chownOutput;
                ExecCommand(chownCmd, chownOutput);
            }

            auto cfgResult = GenerateConfig(config);
            if (!cfgResult.IsDefalutSuccess()) {
                return MakeError("Failed to generate MySQL config: " + cfgResult.msg);
            }

            // MariaDB 10.x 使用 mysql_install_db（在 scripts/ 或 bin/ 目录），MySQL 8.x 使用 mysqld
            // --initialize-insecure
            std::string initCmd;
            std::string installDbPath;
            if (fs::exists(config.binDir + "/scripts/mysql_install_db")) {
                installDbPath = config.binDir + "/scripts/mysql_install_db";
            } else if (fs::exists(config.binDir + "/bin/mysql_install_db")) {
                installDbPath = config.binDir + "/bin/mysql_install_db";
            }

            if (!installDbPath.empty()) {
                // MariaDB 10.x 初始化方式
                initCmd = installDbPath + " --defaults-file=" + config.dataDir +
                          "/my.cnf"
                          " --basedir=" +
                          config.binDir + " --datadir=" + config.dataDir;
                // 仅当指定 osUser 时才以该用户身份初始化
                if (!config.osUser.empty()) {
                    initCmd += " --user=" + config.osUser;
                }
            } else {
                // MySQL 8.x 初始化方式
                initCmd = config.binDir +
                          "/bin/mysqld"
                          " --defaults-file=" +
                          config.dataDir +
                          "/my.cnf"
                          " --initialize-insecure";
            }
            std::string output;
            int ret = ExecCommand(initCmd, output);
            if (ret != 0) {
                return MakeError("MySQL initialize failed: " + output);
            }

            return MakeSuccess();
        }
        // NOLINTEND(readability-function-size, readability-function-cognitive-complexity)

        ResultMsg MySQLBackend::Start(const DatabaseConfig &config) {
            if (IsRunning(config)) {
                return MakeWarning("MySQL is already running on port " + std::to_string(GetEffectivePort(config)));
            }

            // 后台启动 mysqld，输出重定向到日志文件
            // 必须使用 system() 而非 popen()，因为 popen 的管道 fd 会被 mysqld 继承导致阻塞
            std::string logFile = config.dataDir + "/mysqld.log";
            std::string startCmd = config.binDir +
                                   "/bin/mysqld"
                                   " --defaults-file=" +
                                   config.dataDir +
                                   "/my.cnf"
                                   " > " +
                                   logFile + " 2>&1 &";
            int ret = system(startCmd.c_str());
            if (ret != 0) {
                return MakeError("Failed to start mysqld");
            }

            // 等待端口就绪，超时 60 秒
            int port = GetEffectivePort(config);
            if (!WaitForPort(port, 60)) {
                return MakeError("MySQL start timeout, port " + std::to_string(port) + " not ready");
            }

            return MakeSuccess();
        }

        ResultMsg MySQLBackend::Stop(const DatabaseConfig &config) {
            if (!IsRunning(config)) {
                return MakeWarning("MySQL is not running");
            }

            // 通过 mysqladmin 关闭数据库
            std::ostringstream oss;
            oss << config.binDir << "/bin/mysqladmin" << " -u " << config.adminUser;
            if (!config.adminPwd.empty()) {
                oss << " -p" << config.adminPwd;
            }
            oss << " --socket=" << GetSocketPath(config) << " shutdown";

            std::string output;
            int ret = ExecCommand(oss.str(), output);
            if (ret != 0) {
                // mysqladmin 可能失败，但数据库可能已关闭，重新检查
                if (!IsRunning(config)) {
                    return MakeSuccess();
                }
                return MakeError("Failed to stop MySQL: " + output);
            }

            return MakeSuccess();
        }

        bool MySQLBackend::IsRunning(const DatabaseConfig &config) const {
            int port = GetEffectivePort(config);
            return TcpConnect(port);
        }

        ResultMsg MySQLBackend::ExecuteSQL(const DatabaseConfig &config, const std::string &sql,
                                           const std::string &dbName) {
            if (sql.empty()) {
                return MakeError("SQL statement is empty");
            }

            std::string clientCmd = GetMySQLClientCmd(config, dbName);
            return ExecuteSQLViaTempFile(clientCmd, sql);
        }

        ResultMsg MySQLBackend::CreateUser(const DatabaseConfig &config, const std::string &username,
                                           const std::string &password) {
            if (username.empty()) {
                return MakeError("Username is empty");
            }

            std::string sql = "CREATE USER IF NOT EXISTS '" + username + "'@'%' IDENTIFIED BY '" + password +
                              "';\n"
                              "FLUSH PRIVILEGES;\n";

            std::string clientCmd = GetMySQLClientCmd(config, "");
            return ExecuteSQLViaTempFile(clientCmd, sql);
        }

        ResultMsg MySQLBackend::DeleteUser(const DatabaseConfig &config, const std::string &username) {
            if (username.empty()) {
                return MakeError("Username is empty");
            }

            std::string sql = "DROP USER IF EXISTS '" + username +
                              "'@'%';\n"
                              "FLUSH PRIVILEGES;\n";

            std::string clientCmd = GetMySQLClientCmd(config, "");
            return ExecuteSQLViaTempFile(clientCmd, sql);
        }

        ResultMsg MySQLBackend::CreateDatabase(const DatabaseConfig &config, const std::string &dbName) {
            if (dbName.empty()) {
                return MakeError("Database name is empty");
            }

            std::string sql = "CREATE DATABASE IF NOT EXISTS `" + dbName + "` DEFAULT CHARACTER SET utf8mb4;\n";

            std::string clientCmd = GetMySQLClientCmd(config, "");
            return ExecuteSQLViaTempFile(clientCmd, sql);
        }

        ResultMsg MySQLBackend::DeleteDatabase(const DatabaseConfig &config, const std::string &dbName) {
            if (dbName.empty()) {
                return MakeError("Database name is empty");
            }

            std::string sql = "DROP DATABASE IF EXISTS `" + dbName + "`;\n";

            std::string clientCmd = GetMySQLClientCmd(config, "");
            return ExecuteSQLViaTempFile(clientCmd, sql);
        }

        ResultMsg MySQLBackend::CreateTable(const DatabaseConfig &config, const std::string &dbName,
                                            const std::string &tableName, const std::string &columns) {
            if (dbName.empty()) {
                return MakeError("Database name is empty");
            }
            if (tableName.empty()) {
                return MakeError("Table name is empty");
            }
            if (columns.empty()) {
                return MakeError("Columns definition is empty");
            }

            std::string sql = "CREATE TABLE IF NOT EXISTS `" + tableName + "` (" + columns + ");\n";

            std::string clientCmd = GetMySQLClientCmd(config, dbName);
            return ExecuteSQLViaTempFile(clientCmd, sql);
        }

        ResultMsg MySQLBackend::DropTable(const DatabaseConfig &config, const std::string &dbName,
                                          const std::string &tableName) {
            if (dbName.empty()) {
                return MakeError("Database name is empty");
            }
            if (tableName.empty()) {
                return MakeError("Table name is empty");
            }

            std::string sql = "DROP TABLE IF EXISTS `" + tableName + "`;\n";

            std::string clientCmd = GetMySQLClientCmd(config, dbName);
            return ExecuteSQLViaTempFile(clientCmd, sql);
        }

        ResultMsg MySQLBackend::SetDataDir(DatabaseConfig &config, const std::string &dataDir) {
            if (dataDir.empty()) {
                return MakeError("Data directory path is empty");
            }

            if (!fs::exists(dataDir)) {
                return MakeError("Data directory does not exist: " + dataDir);
            }

            // 已初始化的数据库不允许修改数据目录，否则数据将无法访问
            if (fs::exists(config.dataDir + "/ibdata1")) {
                return MakeError("Cannot change dataDir after MySQL is initialized");
            }

            config.dataDir = utils::GetAbsolutePath(dataDir);
            return MakeSuccess();
        }

        // NOLINTBEGIN(readability-function-size, readability-function-cognitive-complexity)
        // 函数大小和复杂度超过阈值，但符合业务逻辑
        ResultMsg MySQLBackend::ExecuteInitScripts(const DatabaseConfig &config) {
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
                return MakeWarning("sqlDir does not exist, skip init scripts: " + sqlDirPath.string());
            }

            // 扫描 sqlDir 下所有 .sql 文件，按文件名排序
            std::vector<fs::path> sqlFiles;
            for (const auto &entry : fs::directory_iterator(sqlDirPath)) {
                if (entry.is_regular_file() && entry.path().extension() == ".sql") {
                    sqlFiles.push_back(entry.path());
                }
            }

            if (sqlFiles.empty()) {
                return MakeWarning("No .sql files found in sqlDir: " + sqlDirPath.string());
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
                        return MakeError("Failed to read SQL script: " + sqlFile.string());
                    }
                    // 文件存在但内容为空，跳过
                    continue;
                }

                auto result = ExecuteSQL(config, content, "");
                if (!result.IsDefalutSuccess()) {
                    return MakeError("Failed to execute " + sqlFile.filename().string() + ": " + result.msg);
                }
            }

            return MakeSuccess();
        }
        // NOLINTEND(readability-function-size, readability-function-cognitive-complexity)

    }  // namespace scm
}  // namespace qifeng
