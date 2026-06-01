/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "dbinit/database_backend.h"
#include "dbinit/database_init.h"
#include "dbinit/mysql_backend.h"
#include "dbinit/opengauss_backend.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <thread>

using namespace qifeng::scm;

namespace {
    // 测试用临时目录前缀
    const std::string TEST_TMP_BASE = "/tmp/scm_db_test";

    // MariaDB 测试根目录
    const std::string MARIADB_TEST_BASE = "/tmp/scm_mariadb_test";

    // MariaDB tar 包路径
    const std::string MARIADB_TAR_PATH = "/home/lyh/code/cppProgram/mariadb-10.3.39-linux-x86_64.tar.gz";

    // 构建一个合法的 MySQL 配置
    DatabaseConfig MakeMySQLConfig() {
        DatabaseConfig cfg;
        cfg.dbInfo.dbType = DatabaseType::MYSQL;
        cfg.dbInfo.sqlDir = TEST_TMP_BASE + "/mysql/sql";
        cfg.binDir = TEST_TMP_BASE + "/mysql/bin";
        cfg.dataDir = TEST_TMP_BASE + "/mysql/data";
        cfg.port = 13306;
        cfg.adminUser = "root";
        cfg.adminPwd = "Root@Test2024!";
        cfg.osUser = "mysql";
        cfg.osGroup = "mysql";
        return cfg;
    }

    // 构建一个合法的 OpenGauss 配置
    DatabaseConfig MakeOpenGaussConfig() {
        DatabaseConfig cfg;
        cfg.dbInfo.dbType = DatabaseType::OPENGAUSS;
        cfg.dbInfo.sqlDir = TEST_TMP_BASE + "/opengauss/sql";
        cfg.binDir = TEST_TMP_BASE + "/opengauss/bin";
        cfg.dataDir = TEST_TMP_BASE + "/opengauss/data";
        cfg.port = 15432;
        cfg.adminUser = "omm";
        cfg.adminPwd = "Omm@Test2024!";
        cfg.osUser = "omm";
        cfg.osGroup = "dbgrp";
        return cfg;
    }

    // 辅助：创建包含指定内容的文件
    bool CreateFileWithContent(const std::string &path, const std::string &content) {
        std::ofstream ofs(path);
        if (!ofs)
            return false;
        ofs << content;
        return ofs.good();
    }
}  // namespace

// ============================================================================
// 1. DatabaseConfig 默认值测试
// ============================================================================

TEST(DatabaseConfigTest, DefaultValues) {
    DatabaseConfig cfg;
    EXPECT_EQ(cfg.dbInfo.dbType, DatabaseType::MYSQL);
    EXPECT_TRUE(cfg.dbInfo.sqlDir.empty());
    EXPECT_TRUE(cfg.binDir.empty());
    EXPECT_TRUE(cfg.dataDir.empty());
    EXPECT_EQ(cfg.port, 0);
    EXPECT_TRUE(cfg.adminUser.empty());
    EXPECT_TRUE(cfg.adminPwd.empty());
    EXPECT_TRUE(cfg.osUser.empty());
    EXPECT_TRUE(cfg.osGroup.empty());
}

// ============================================================================
// 2. DatabaseInit 构造测试
// ============================================================================

TEST(DatabaseInitTest, ConstructMySQL) {
    auto cfg = MakeMySQLConfig();
    DatabaseInit dbInit(cfg);
    EXPECT_EQ(dbInit.GetConfig().dbInfo.dbType, DatabaseType::MYSQL);
    EXPECT_EQ(dbInit.GetConfig().port, 13306);
}

TEST(DatabaseInitTest, ConstructOpenGauss) {
    auto cfg = MakeOpenGaussConfig();
    DatabaseInit dbInit(cfg);
    EXPECT_EQ(dbInit.GetConfig().dbInfo.dbType, DatabaseType::OPENGAUSS);
    EXPECT_EQ(dbInit.GetConfig().port, 15432);
}

// ============================================================================
// 3. GetConfig 测试
// ============================================================================

TEST(DatabaseInitTest, GetConfig) {
    auto cfg = MakeMySQLConfig();
    DatabaseInit dbInit(cfg);
    const auto &returnedCfg = dbInit.GetConfig();
    EXPECT_EQ(returnedCfg.dbInfo.dbType, cfg.dbInfo.dbType);
    EXPECT_EQ(returnedCfg.binDir, cfg.binDir);
    EXPECT_EQ(returnedCfg.dataDir, cfg.dataDir);
    EXPECT_EQ(returnedCfg.port, cfg.port);
    EXPECT_EQ(returnedCfg.adminUser, cfg.adminUser);
    EXPECT_EQ(returnedCfg.adminPwd, cfg.adminPwd);
    EXPECT_EQ(returnedCfg.osUser, cfg.osUser);
    EXPECT_EQ(returnedCfg.osGroup, cfg.osGroup);
}

// ============================================================================
// 4. MySQL 后端 - 参数校验测试
// ============================================================================

class MySQLBackendTest : public ::testing::Test {
protected:
    void SetUp() override { cfg_ = MakeMySQLConfig(); }

    void TearDown() override {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::remove_all(TEST_TMP_BASE, ec);
    }

    DatabaseConfig cfg_;
};

TEST_F(MySQLBackendTest, Initialize_EmptyBinDir) {
    cfg_.binDir.clear();
    MySQLBackend backend;
    auto result = backend.Initialize(cfg_);
    EXPECT_FALSE(result.IsDefalutSuccess());
    EXPECT_NE(result.msg.find("binDir"), std::string::npos);
}

TEST_F(MySQLBackendTest, Initialize_EmptyDataDir) {
    cfg_.dataDir.clear();
    MySQLBackend backend;
    auto result = backend.Initialize(cfg_);
    EXPECT_FALSE(result.IsDefalutSuccess());
    EXPECT_NE(result.msg.find("dataDir"), std::string::npos);
}

TEST_F(MySQLBackendTest, ExecuteSQL_EmptySQL) {
    MySQLBackend backend;
    auto result = backend.ExecuteSQL(cfg_, "", "");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(MySQLBackendTest, CreateUser_EmptyUsername) {
    MySQLBackend backend;
    auto result = backend.CreateUser(cfg_, "", "password");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(MySQLBackendTest, DeleteUser_EmptyUsername) {
    MySQLBackend backend;
    auto result = backend.DeleteUser(cfg_, "");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(MySQLBackendTest, CreateDatabase_EmptyName) {
    MySQLBackend backend;
    auto result = backend.CreateDatabase(cfg_, "");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(MySQLBackendTest, DeleteDatabase_EmptyName) {
    MySQLBackend backend;
    auto result = backend.DeleteDatabase(cfg_, "");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(MySQLBackendTest, CreateTable_EmptyDbName) {
    MySQLBackend backend;
    auto result = backend.CreateTable(cfg_, "", "t", "id INT");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(MySQLBackendTest, CreateTable_EmptyTableName) {
    MySQLBackend backend;
    auto result = backend.CreateTable(cfg_, "db", "", "id INT");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(MySQLBackendTest, CreateTable_EmptyColumns) {
    MySQLBackend backend;
    auto result = backend.CreateTable(cfg_, "db", "t", "");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(MySQLBackendTest, DropTable_EmptyDbName) {
    MySQLBackend backend;
    auto result = backend.DropTable(cfg_, "", "t");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(MySQLBackendTest, DropTable_EmptyTableName) {
    MySQLBackend backend;
    auto result = backend.DropTable(cfg_, "db", "");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(MySQLBackendTest, SetDataDir_EmptyPath) {
    MySQLBackend backend;
    auto result = backend.SetDataDir(cfg_, "");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(MySQLBackendTest, SetDataDir_NonExistentPath) {
    MySQLBackend backend;
    auto result = backend.SetDataDir(cfg_, "/nonexistent/path/12345");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(MySQLBackendTest, IsRunning_NotRunning) {
    MySQLBackend backend;
    cfg_.port = 19999;
    EXPECT_FALSE(backend.IsRunning(cfg_));
}

// ============================================================================
// 5. MySQL 后端 - SetDataDir 成功路径测试
// ============================================================================

TEST_F(MySQLBackendTest, SetDataDir_Success) {
    namespace fs = std::filesystem;
    std::string newDataDir = TEST_TMP_BASE + "/mysql/new_data";
    fs::create_directories(newDataDir);

    MySQLBackend backend;
    auto result = backend.SetDataDir(cfg_, newDataDir);
    EXPECT_TRUE(result.IsDefalutSuccess()) << "msg: " << result.msg;
    EXPECT_FALSE(cfg_.dataDir.empty());
}

TEST_F(MySQLBackendTest, SetDataDir_AlreadyInitialized) {
    namespace fs = std::filesystem;
    fs::create_directories(cfg_.dataDir);
    {
        std::ofstream ofs(cfg_.dataDir + "/ibdata1");
        ofs << "fake";
    }

    std::string newDataDir = TEST_TMP_BASE + "/mysql/new_data";
    fs::create_directories(newDataDir);

    MySQLBackend backend;
    auto result = backend.SetDataDir(cfg_, newDataDir);
    EXPECT_FALSE(result.IsDefalutSuccess());
    EXPECT_NE(result.msg.find("initialized"), std::string::npos);
}

// ============================================================================
// 6. MySQL 后端 - ExecuteInitScripts 单元测试
// ============================================================================

TEST_F(MySQLBackendTest, ExecuteInitScripts_EmptySqlDir) {
    cfg_.dbInfo.sqlDir.clear();
    MySQLBackend backend;
    auto result = backend.ExecuteInitScripts(cfg_);
    EXPECT_TRUE(result.IsDefalutSuccess()) << "msg: " << result.msg;
}

TEST_F(MySQLBackendTest, ExecuteInitScripts_SqlDirNotExist) {
    cfg_.dbInfo.sqlDir = "/nonexistent/sql/dir";
    MySQLBackend backend;
    auto result = backend.ExecuteInitScripts(cfg_);
    // 目录不存在应返回警告（code=1），不是错误
    EXPECT_EQ(result.code, 1);
}

TEST_F(MySQLBackendTest, ExecuteInitScripts_NoSqlFiles) {
    namespace fs = std::filesystem;
    std::string sqlDir = TEST_TMP_BASE + "/mysql/empty_sql";
    fs::create_directories(sqlDir);
    cfg_.dbInfo.sqlDir = sqlDir;

    MySQLBackend backend;
    auto result = backend.ExecuteInitScripts(cfg_);
    // 无 .sql 文件应返回警告
    EXPECT_EQ(result.code, 1);
}

TEST_F(MySQLBackendTest, ExecuteInitScripts_WithSqlFiles) {
    namespace fs = std::filesystem;
    std::string sqlDir = TEST_TMP_BASE + "/mysql/sql_with_files";
    fs::create_directories(sqlDir);

    // 创建测试 SQL 脚本文件
    CreateFileWithContent(sqlDir + "/01_create_db.sql",
                          "CREATE DATABASE IF NOT EXISTS test_app DEFAULT CHARACTER SET utf8mb4;");
    CreateFileWithContent(sqlDir + "/02_create_table.sql",
                          "CREATE TABLE IF NOT EXISTS test_app.users (id INT PRIMARY KEY, name VARCHAR(100));");

    cfg_.dbInfo.sqlDir = sqlDir;

    // 数据库未启动，执行脚本会失败（因为无法连接）
    MySQLBackend backend;
    auto result = backend.ExecuteInitScripts(cfg_);
    // 由于数据库未运行，ExecuteSQL 会失败
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(MySQLBackendTest, ExecuteInitScripts_EmptySqlFile) {
    namespace fs = std::filesystem;
    std::string sqlDir = TEST_TMP_BASE + "/mysql/sql_empty_file";
    fs::create_directories(sqlDir);

    // 创建空的 .sql 文件
    CreateFileWithContent(sqlDir + "/01_empty.sql", "");

    cfg_.dbInfo.sqlDir = sqlDir;

    MySQLBackend backend;
    auto result = backend.ExecuteInitScripts(cfg_);
    // 空文件被跳过，所有文件处理完毕后返回成功（无错误发生）
    EXPECT_TRUE(result.IsDefalutSuccess()) << "msg: " << result.msg;
}

TEST_F(MySQLBackendTest, ExecuteInitScripts_RelativeSqlDir) {
    namespace fs = std::filesystem;
    // 创建 binDir 下的相对 sql 目录
    std::string absSqlDir = TEST_TMP_BASE + "/mysql/bin/sql";
    fs::create_directories(absSqlDir);

    cfg_.dbInfo.sqlDir = "sql";  // 相对路径
    cfg_.binDir = TEST_TMP_BASE + "/mysql/bin";

    MySQLBackend backend;
    auto result = backend.ExecuteInitScripts(cfg_);
    // 目录存在但无 .sql 文件，返回警告
    EXPECT_EQ(result.code, 1);
}

// ============================================================================
// 7. OpenGauss 后端 - 参数校验测试
// ============================================================================

class OpenGaussBackendTest : public ::testing::Test {
protected:
    void SetUp() override { cfg_ = MakeOpenGaussConfig(); }

    void TearDown() override {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::remove_all(TEST_TMP_BASE, ec);
    }

    DatabaseConfig cfg_;
};

TEST_F(OpenGaussBackendTest, Initialize_EmptyBinDir) {
    cfg_.binDir.clear();
    OpenGaussBackend backend;
    auto result = backend.Initialize(cfg_);
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(OpenGaussBackendTest, Initialize_EmptyDataDir) {
    cfg_.dataDir.clear();
    OpenGaussBackend backend;
    auto result = backend.Initialize(cfg_);
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(OpenGaussBackendTest, Initialize_EmptyOsUser) {
    cfg_.osUser.clear();
    OpenGaussBackend backend;
    auto result = backend.Initialize(cfg_);
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(OpenGaussBackendTest, Initialize_EmptyAdminPwd) {
    cfg_.adminPwd.clear();
    OpenGaussBackend backend;
    auto result = backend.Initialize(cfg_);
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(OpenGaussBackendTest, ExecuteSQL_EmptySQL) {
    OpenGaussBackend backend;
    auto result = backend.ExecuteSQL(cfg_, "", "");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(OpenGaussBackendTest, CreateUser_EmptyUsername) {
    OpenGaussBackend backend;
    auto result = backend.CreateUser(cfg_, "", "password");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(OpenGaussBackendTest, DeleteUser_EmptyUsername) {
    OpenGaussBackend backend;
    auto result = backend.DeleteUser(cfg_, "");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(OpenGaussBackendTest, CreateDatabase_EmptyName) {
    OpenGaussBackend backend;
    auto result = backend.CreateDatabase(cfg_, "");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(OpenGaussBackendTest, DeleteDatabase_EmptyName) {
    OpenGaussBackend backend;
    auto result = backend.DeleteDatabase(cfg_, "");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(OpenGaussBackendTest, CreateTable_EmptyDbName) {
    OpenGaussBackend backend;
    auto result = backend.CreateTable(cfg_, "", "t", "id INT");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(OpenGaussBackendTest, CreateTable_EmptyTableName) {
    OpenGaussBackend backend;
    auto result = backend.CreateTable(cfg_, "db", "", "id INT");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(OpenGaussBackendTest, CreateTable_EmptyColumns) {
    OpenGaussBackend backend;
    auto result = backend.CreateTable(cfg_, "db", "t", "");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(OpenGaussBackendTest, DropTable_EmptyDbName) {
    OpenGaussBackend backend;
    auto result = backend.DropTable(cfg_, "", "t");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(OpenGaussBackendTest, DropTable_EmptyTableName) {
    OpenGaussBackend backend;
    auto result = backend.DropTable(cfg_, "db", "");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(OpenGaussBackendTest, SetDataDir_EmptyPath) {
    OpenGaussBackend backend;
    auto result = backend.SetDataDir(cfg_, "");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(OpenGaussBackendTest, SetDataDir_RelativePath) {
    OpenGaussBackend backend;
    auto result = backend.SetDataDir(cfg_, "relative/path");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(OpenGaussBackendTest, IsRunning_NotRunning) {
    OpenGaussBackend backend;
    cfg_.port = 19998;
    EXPECT_FALSE(backend.IsRunning(cfg_));
}

// ============================================================================
// 8. OpenGauss 后端 - SetDataDir 成功路径测试
// ============================================================================

TEST_F(OpenGaussBackendTest, SetDataDir_Success) {
    namespace fs = std::filesystem;
    std::string newDataDir = TEST_TMP_BASE + "/opengauss/new_data";
    fs::create_directories(newDataDir);

    OpenGaussBackend backend;
    auto result = backend.SetDataDir(cfg_, newDataDir);
    EXPECT_TRUE(result.IsDefalutSuccess()) << "msg: " << result.msg;
    EXPECT_EQ(cfg_.dataDir, newDataDir);
}

TEST_F(OpenGaussBackendTest, SetDataDir_AlreadyInitialized) {
    namespace fs = std::filesystem;
    std::string newDataDir = TEST_TMP_BASE + "/opengauss/new_data";
    fs::create_directories(newDataDir);
    {
        std::ofstream ofs(newDataDir + "/PG_VERSION");
        ofs << "9";
    }

    OpenGaussBackend backend;
    auto result = backend.SetDataDir(cfg_, newDataDir);
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(OpenGaussBackendTest, SetDataDir_CreatesDirectoryAndSetsPermissions) {
    namespace fs = std::filesystem;
    std::string newDataDir = TEST_TMP_BASE + "/opengauss/auto_created";
    fs::remove_all(newDataDir);

    OpenGaussBackend backend;
    auto result = backend.SetDataDir(cfg_, newDataDir);
    EXPECT_TRUE(result.IsDefalutSuccess()) << "msg: " << result.msg;
    EXPECT_TRUE(fs::exists(newDataDir));
}

// ============================================================================
// 9. OpenGauss 后端 - ExecuteInitScripts 单元测试
// ============================================================================

TEST_F(OpenGaussBackendTest, ExecuteInitScripts_EmptySqlDir) {
    cfg_.dbInfo.sqlDir.clear();
    OpenGaussBackend backend;
    auto result = backend.ExecuteInitScripts(cfg_);
    EXPECT_TRUE(result.IsDefalutSuccess()) << "msg: " << result.msg;
}

TEST_F(OpenGaussBackendTest, ExecuteInitScripts_SqlDirNotExist) {
    cfg_.dbInfo.sqlDir = "/nonexistent/sql/dir";
    OpenGaussBackend backend;
    auto result = backend.ExecuteInitScripts(cfg_);
    EXPECT_EQ(result.code, 1);
}

TEST_F(OpenGaussBackendTest, ExecuteInitScripts_NoSqlFiles) {
    namespace fs = std::filesystem;
    std::string sqlDir = TEST_TMP_BASE + "/opengauss/empty_sql";
    fs::create_directories(sqlDir);
    cfg_.dbInfo.sqlDir = sqlDir;

    OpenGaussBackend backend;
    auto result = backend.ExecuteInitScripts(cfg_);
    EXPECT_EQ(result.code, 1);
}

// ============================================================================
// 10. DatabaseInit 通过统一接口测试（参数校验）
// ============================================================================

TEST(DatabaseInitInterfaceTest, Initialize_EmptyBinDir) {
    auto cfg = MakeMySQLConfig();
    cfg.binDir.clear();
    DatabaseInit dbInit(cfg);
    auto result = dbInit.Initialize();
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST(DatabaseInitInterfaceTest, Initialize_EmptyDataDir) {
    auto cfg = MakeMySQLConfig();
    cfg.dataDir.clear();
    DatabaseInit dbInit(cfg);
    auto result = dbInit.Initialize();
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST(DatabaseInitInterfaceTest, CreateUser_EmptyUsername) {
    auto cfg = MakeMySQLConfig();
    DatabaseInit dbInit(cfg);
    auto result = dbInit.CreateUser("", "password");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST(DatabaseInitInterfaceTest, DeleteUser_EmptyUsername) {
    auto cfg = MakeMySQLConfig();
    DatabaseInit dbInit(cfg);
    auto result = dbInit.DeleteUser("");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST(DatabaseInitInterfaceTest, CreateDatabase_EmptyName) {
    auto cfg = MakeMySQLConfig();
    DatabaseInit dbInit(cfg);
    auto result = dbInit.CreateDatabase("");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST(DatabaseInitInterfaceTest, DeleteDatabase_EmptyName) {
    auto cfg = MakeMySQLConfig();
    DatabaseInit dbInit(cfg);
    auto result = dbInit.DeleteDatabase("");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST(DatabaseInitInterfaceTest, CreateTable_EmptyDbName) {
    auto cfg = MakeMySQLConfig();
    DatabaseInit dbInit(cfg);
    auto result = dbInit.CreateTable("", "t", "id INT");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST(DatabaseInitInterfaceTest, DropTable_EmptyTableName) {
    auto cfg = MakeMySQLConfig();
    DatabaseInit dbInit(cfg);
    auto result = dbInit.DropTable("db", "");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST(DatabaseInitInterfaceTest, ExecuteSQL_EmptySQL) {
    auto cfg = MakeMySQLConfig();
    DatabaseInit dbInit(cfg);
    auto result = dbInit.ExecuteSQL("");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST(DatabaseInitInterfaceTest, SetDataDir_EmptyPath) {
    auto cfg = MakeMySQLConfig();
    DatabaseInit dbInit(cfg);
    auto result = dbInit.SetDataDir("");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST(DatabaseInitInterfaceTest, IsRunning_NotRunning) {
    auto cfg = MakeMySQLConfig();
    cfg.port = 19997;
    DatabaseInit dbInit(cfg);
    EXPECT_FALSE(dbInit.IsRunning());
}

TEST(DatabaseInitInterfaceTest, ExecuteInitScripts_EmptySqlDir) {
    auto cfg = MakeMySQLConfig();
    cfg.dbInfo.sqlDir.clear();
    DatabaseInit dbInit(cfg);
    auto result = dbInit.ExecuteInitScripts();
    EXPECT_TRUE(result.IsDefalutSuccess()) << "msg: " << result.msg;
}

// ============================================================================
// 11. MySQL 静态方法测试
// ============================================================================

TEST(MySQLBackendStaticTest, GetDefaultPort) {
    EXPECT_EQ(MySQLBackend::GetDefaultPort(), 3306);
}

TEST(MySQLBackendStaticTest, GetEffectivePort_Zero) {
    DatabaseConfig cfg;
    cfg.port = 0;
    EXPECT_EQ(MySQLBackend::GetEffectivePort(cfg), 3306);
}

TEST(MySQLBackendStaticTest, GetEffectivePort_Custom) {
    DatabaseConfig cfg;
    cfg.port = 13306;
    EXPECT_EQ(MySQLBackend::GetEffectivePort(cfg), 13306);
}

// ============================================================================
// 12. OpenGauss 静态方法测试
// ============================================================================

TEST(OpenGaussBackendStaticTest, GetDefaultPort) {
    EXPECT_EQ(OpenGaussBackend::GetDefaultPort(), 5432);
}

TEST(OpenGaussBackendStaticTest, GetEffectivePort_Zero) {
    DatabaseConfig cfg;
    cfg.port = 0;
    EXPECT_EQ(OpenGaussBackend::GetEffectivePort(cfg), 5432);
}

TEST(OpenGaussBackendStaticTest, GetEffectivePort_Custom) {
    DatabaseConfig cfg;
    cfg.port = 15432;
    EXPECT_EQ(OpenGaussBackend::GetEffectivePort(cfg), 15432);
}

// ============================================================================
// 13. MariaDB 集成测试（以当前用户身份运行，无需 root）
//
// 运行方式：
//   ./bin/test_database --gtest_also_run_disabled_tests --gtest_filter="MariaDBIntegrationTest*"
//
// 前提条件：
//   1. 安装 libaio：sudo apt install -y libaio1t64
//   2. 创建符号链接：sudo ln -s /usr/lib/x86_64-linux-gnu/libaio.so.1t64 /usr/lib/x86_64-linux-gnu/libaio.so.1
// 注意：
//   测试 SetUp 中会自动处理旧版 MariaDB 的 libtinfo.so.5 依赖兼容性问题
//   （现代系统提供 libtinfo.so.6，测试会创建兼容符号链接并设置 LD_LIBRARY_PATH）
// ============================================================================

class MariaDBIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        namespace fs = std::filesystem;

        // 强制杀掉占用端口的残留 mysqld 进程
        int fuserRet = system("fuser -k 23306/tcp 2>/dev/null || true");
        (void)fuserRet;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // 清理旧测试目录
        std::error_code ec;
        fs::remove_all(MARIADB_TEST_BASE, ec);

        // 解压 MariaDB tar 包
        fs::create_directories(MARIADB_TEST_BASE);
        std::string extractCmd = "tar -xzf " + MARIADB_TAR_PATH + " -C " + MARIADB_TEST_BASE;
        ASSERT_EQ(system(extractCmd.c_str()), 0) << "解压 MariaDB 失败";

        // 查找解压后的目录名
        bool foundBinDir = false;
        for (const auto &entry : fs::directory_iterator(MARIADB_TEST_BASE)) {
            if (fs::exists(entry.path() / "bin" / "mysqld")) {
                mariadbBinDir_ = entry.path().string();
                foundBinDir = true;
                break;
            }
        }
        ASSERT_TRUE(foundBinDir) << "未找到 MariaDB 二进制目录";

        // 修复旧版 MariaDB 二进制在新系统上的 libtinfo.so.5 依赖缺失问题
        // 现代系统（如 Ubuntu 24.04）提供 libtinfo.so.6，创建兼容符号链接
        std::string libFixDir = MARIADB_TEST_BASE + "/lib_fix";
        fs::create_directories(libFixDir);
        std::string libtinfoFix = libFixDir + "/libtinfo.so.5";
        if (!fs::exists(libtinfoFix)) {
            std::string linkCmd = "ln -s /lib/x86_64-linux-gnu/libtinfo.so.6 " + libtinfoFix;
            int linkRet = system(linkCmd.c_str());
            (void)linkRet;
        }
        // 将修复目录加入 LD_LIBRARY_PATH
        const char* currentLdPath = getenv("LD_LIBRARY_PATH");
        std::string newLdPath = libFixDir;
        if (currentLdPath != nullptr && strlen(currentLdPath) > 0) {
            newLdPath = newLdPath + ":" + currentLdPath;
        }
        setenv("LD_LIBRARY_PATH", newLdPath.c_str(), 1);

        // 配置 DatabaseConfig：osUser 为空表示以当前用户身份运行，无需 root
        cfg_.dbInfo.dbType = DatabaseType::MYSQL;
        cfg_.binDir = mariadbBinDir_;
        cfg_.dataDir = MARIADB_TEST_BASE + "/data";
        cfg_.dbInfo.sqlDir = MARIADB_TEST_BASE + "/sql";
        cfg_.port = 23306;
        cfg_.adminUser = "root";
        cfg_.adminPwd = "";
        cfg_.osUser = "";
        cfg_.osGroup = "";

        // 创建 SQL 脚本目录和文件
        fs::create_directories(cfg_.dbInfo.sqlDir);
        CreateFileWithContent(cfg_.dbInfo.sqlDir + "/01_create_database.sql",
                              "CREATE DATABASE IF NOT EXISTS test_app DEFAULT CHARACTER SET utf8mb4;");
        CreateFileWithContent(cfg_.dbInfo.sqlDir + "/02_create_table.sql", "CREATE TABLE IF NOT EXISTS test_app.users ("
                                                                           "id INT PRIMARY KEY AUTO_INCREMENT, "
                                                                           "name VARCHAR(100) NOT NULL, "
                                                                           "email VARCHAR(255));");
        CreateFileWithContent(cfg_.dbInfo.sqlDir + "/03_init_data.sql", "INSERT INTO test_app.users (name, email) "
                                                                        "VALUES ('admin', 'admin@test.com');");
    }

    void TearDown() override {
        // 尝试优雅关闭数据库
        DatabaseInit dbInit(cfg_);
        dbInit.Stop();

        // 强制杀掉残留进程
        int fuserRet = system("fuser -k 23306/tcp 2>/dev/null || true");
        (void)fuserRet;

        // 清理测试目录
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::remove_all(MARIADB_TEST_BASE, ec);
    }

    DatabaseConfig cfg_;
    std::string mariadbBinDir_;
};

TEST_F(MariaDBIntegrationTest, DISABLED_FullWorkflow) {
    DatabaseInit dbInit(cfg_);

    // 1. 初始化数据库
    auto result = dbInit.Initialize();
    ASSERT_TRUE(result.IsDefalutSuccess()) << "Initialize: " << result.msg;

    // 2. 启动数据库
    result = dbInit.Start();
    ASSERT_TRUE(result.IsDefalutSuccess()) << "Start: " << result.msg;
    EXPECT_TRUE(dbInit.IsRunning());

    // 3. 执行 SQL 初始化脚本
    result = dbInit.ExecuteInitScripts();
    EXPECT_TRUE(result.IsDefalutSuccess()) << "ExecuteInitScripts: " << result.msg;

    // 4. 验证数据库已创建
    result = dbInit.ExecuteSQL("USE test_app; SELECT COUNT(*) FROM users;", "");
    EXPECT_TRUE(result.IsDefalutSuccess()) << "Verify database: " << result.msg;

    // 5. 验证初始数据
    result = dbInit.ExecuteSQL("SELECT name FROM test_app.users WHERE name='admin';", "");
    EXPECT_TRUE(result.IsDefalutSuccess()) << "Verify data: " << result.msg;

    // 6. 运行时动态操作：创建用户
    result = dbInit.CreateUser("app_user", "App@2024!");
    EXPECT_TRUE(result.IsDefalutSuccess()) << "CreateUser: " << result.msg;

    // 7. 运行时动态操作：创建数据库
    result = dbInit.CreateDatabase("dynamic_db");
    EXPECT_TRUE(result.IsDefalutSuccess()) << "CreateDatabase: " << result.msg;

    // 8. 关闭数据库
    result = dbInit.Stop();
    EXPECT_TRUE(result.IsDefalutSuccess()) << "Stop: " << result.msg;
    EXPECT_FALSE(dbInit.IsRunning());
}

TEST_F(MariaDBIntegrationTest, DISABLED_ExecuteInitScripts_VerifyDatabaseAndTable) {
    DatabaseInit dbInit(cfg_);

    ASSERT_TRUE(dbInit.Initialize().IsDefalutSuccess()) << "Initialize failed";
    ASSERT_TRUE(dbInit.Start().IsDefalutSuccess()) << "Start failed";

    // 执行初始化脚本
    auto result = dbInit.ExecuteInitScripts();
    EXPECT_TRUE(result.IsDefalutSuccess()) << "ExecuteInitScripts: " << result.msg;

    // 验证数据库存在
    result = dbInit.ExecuteSQL("SHOW DATABASES LIKE 'test_app';", "");
    EXPECT_TRUE(result.IsDefalutSuccess()) << "Verify database exists: " << result.msg;

    // 验证表存在
    result = dbInit.ExecuteSQL("SHOW TABLES FROM test_app LIKE 'users';", "");
    EXPECT_TRUE(result.IsDefalutSuccess()) << "Verify table exists: " << result.msg;

    // 验证初始数据
    result = dbInit.ExecuteSQL("SELECT COUNT(*) FROM test_app.users;", "");
    EXPECT_TRUE(result.IsDefalutSuccess()) << "Verify data: " << result.msg;

    dbInit.Stop();
}

TEST_F(MariaDBIntegrationTest, DISABLED_Initialize_AlreadyInitialized) {
    DatabaseInit dbInit(cfg_);

    auto result = dbInit.Initialize();
    ASSERT_TRUE(result.IsDefalutSuccess()) << "First init: " << result.msg;

    // 重复初始化应返回警告（幂等）
    result = dbInit.Initialize();
    EXPECT_EQ(result.code, 1);  // warning

    dbInit.Stop();
}

TEST_F(MariaDBIntegrationTest, DISABLED_Start_AlreadyRunning) {
    DatabaseInit dbInit(cfg_);

    ASSERT_TRUE(dbInit.Initialize().IsDefalutSuccess());
    ASSERT_TRUE(dbInit.Start().IsDefalutSuccess());

    auto result = dbInit.Start();
    EXPECT_EQ(result.code, 1);  // warning: already running

    dbInit.Stop();
}

TEST_F(MariaDBIntegrationTest, DISABLED_Stop_NotRunning) {
    DatabaseInit dbInit(cfg_);

    auto result = dbInit.Stop();
    EXPECT_EQ(result.code, 1);  // warning: not running
}
