/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "service_tool/tool_mariadb.h"

#include "common/types.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

namespace fs = std::filesystem;

using namespace qifeng::scm;
using namespace qifeng::scm::tool;

namespace {
    // 测试用临时目录前缀
    const std::string TEST_TMP_BASE = "/tmp/scm_tool_mariadb_test";

    // 辅助：创建包含指定内容的文件
    bool CreateFileWithContent(const std::string &path, const std::string &content) {
        std::ofstream ofs(path);
        if (!ofs)
            return false;
        ofs << content;
        return ofs.good();
    }

    // 构建默认测试配置
    MariadbDef MakeTestMariadbDef() {
        MariadbDef def;
        def.host = "127.0.0.1";
        def.port = "3306";
        def.adminUser = "root";
        def.adminPassword = "Test@123";
        return def;
    }
}  // namespace

// ============================================================================
// 3. 参数校验测试（无需 MariaDB 服务运行）
// ============================================================================

class MariadbTest : public ::testing::Test {
protected:
    void SetUp() override { def_ = MakeTestMariadbDef(); }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(TEST_TMP_BASE, ec);
    }

    MariadbDef def_;
};

// --- CreateUser 参数校验 ---

TEST_F(MariadbTest, CreateUser_EmptyUsername) {
    Mariadb mariadb(def_);
    auto result = mariadb.CreateUser("", "password");
    EXPECT_FALSE(result.IsDefalutSuccess());
    EXPECT_NE(result.msg.find("Username"), std::string::npos);
}

// --- DeleteUserAndDatabase 参数校验 ---

TEST_F(MariadbTest, DeleteUserAndDatabase_EmptyUsername) {
    Mariadb mariadb(def_);
    auto result = mariadb.DeleteUserAndDatabase("");
    EXPECT_FALSE(result.IsDefalutSuccess());
    EXPECT_NE(result.msg.find("Username"), std::string::npos);
}

// --- ExecuteSqlFile 参数校验 ---

TEST_F(MariadbTest, ExecuteSqlFile_EmptyPath) {
    Mariadb mariadb(def_);
    auto result = mariadb.ExecuteSqlFile("");
    EXPECT_FALSE(result.IsDefalutSuccess());
    EXPECT_NE(result.msg.find("SQL file path"), std::string::npos);
}

TEST_F(MariadbTest, ExecuteSqlFile_NonExistentFile) {
    Mariadb mariadb(def_);
    auto result = mariadb.ExecuteSqlFile("/nonexistent/path/test.sql");
    EXPECT_FALSE(result.IsDefalutSuccess());
    EXPECT_NE(result.msg.find("does not exist"), std::string::npos);
}

// --- ExecuteSqlFileByUser 参数校验 ---

TEST_F(MariadbTest, ExecuteSqlFileByUser_EmptyUsername) {
    Mariadb mariadb(def_);
    auto result = mariadb.ExecuteSqlFileByUser("", "password", "/tmp/test.sql");
    EXPECT_FALSE(result.IsDefalutSuccess());
    EXPECT_NE(result.msg.find("Username"), std::string::npos);
}

TEST_F(MariadbTest, ExecuteSqlFileByUser_EmptyPath) {
    Mariadb mariadb(def_);
    auto result = mariadb.ExecuteSqlFileByUser("testuser", "password", "");
    EXPECT_FALSE(result.IsDefalutSuccess());
    EXPECT_NE(result.msg.find("SQL file path"), std::string::npos);
}

TEST_F(MariadbTest, ExecuteSqlFileByUser_NonExistentFile) {
    Mariadb mariadb(def_);
    auto result = mariadb.ExecuteSqlFileByUser("testuser", "password", "/nonexistent/path/test.sql");
    EXPECT_FALSE(result.IsDefalutSuccess());
    EXPECT_NE(result.msg.find("does not exist"), std::string::npos);
}

// ============================================================================
// 4. 文件存在但数据库未运行时的执行测试
// ============================================================================

TEST_F(MariadbTest, ExecuteSqlFile_FileExistsButDbNotRunning) {
    std::string sqlDir = TEST_TMP_BASE + "/sql";
    fs::create_directories(sqlDir);
    std::string sqlFile = sqlDir + "/test.sql";
    CreateFileWithContent(sqlFile, "SELECT 1;");

    Mariadb mariadb(def_);
    auto result = mariadb.ExecuteSqlFile(sqlFile);
    // 文件存在但数据库未运行，执行应失败
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(MariadbTest, ExecuteSqlFileByUser_FileExistsButDbNotRunning) {
    std::string sqlDir = TEST_TMP_BASE + "/sql";
    fs::create_directories(sqlDir);
    std::string sqlFile = sqlDir + "/test.sql";
    CreateFileWithContent(sqlFile, "SELECT 1;");

    Mariadb mariadb(def_);
    auto result = mariadb.ExecuteSqlFileByUser("testuser", "password", sqlFile);
    // 文件存在但数据库未运行，执行应失败
    EXPECT_FALSE(result.IsDefalutSuccess());
}

// ============================================================================
// 5. GetVersion 测试（依赖系统环境）
// ============================================================================

TEST_F(MariadbTest, GetVersion_ReturnStructure) {
    Mariadb mariadb(def_);
    auto result = mariadb.GetVersion();
    // 无论系统是否安装 mysql/mariadb 客户端，返回结构应正确
    if (result.IsDefalutSuccess()) {
        EXPECT_EQ(result.code, 0);
        EXPECT_FALSE(result.msg.empty());
        // 版本号应包含数字
        bool hasDigit = false;
        for (char c : result.msg) {
            if (std::isdigit(static_cast<unsigned char>(c))) {
                hasDigit = true;
                break;
            }
        }
        EXPECT_TRUE(hasDigit) << "Version should contain digits: " << result.msg;
    } else {
        EXPECT_EQ(result.code, -1);
    }
}

// ============================================================================
// 6. 不同配置下的构造和参数校验
// ============================================================================

TEST_F(MariadbTest, CreateUser_EmptyUsernameWithEmptyPassword) {
    Mariadb mariadb(def_);
    auto result = mariadb.CreateUser("", "");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(MariadbTest, ExecuteSqlFileByUser_EmptyUsernameAndPath) {
    Mariadb mariadb(def_);
    auto result = mariadb.ExecuteSqlFileByUser("", "", "");
    EXPECT_FALSE(result.IsDefalutSuccess());
}

TEST_F(MariadbTest, CustomPortConfig) {
    MariadbDef customDef;
    customDef.host = "10.0.0.1";
    customDef.port = "3307";
    customDef.adminUser = "admin";
    customDef.adminPassword = "Admin@2024";
    Mariadb mariadb(customDef);
    // 自定义配置构造成功即可
}

TEST_F(MariadbTest, EmptyAdminPassword) {
    MariadbDef noPwdDef;
    noPwdDef.adminPassword.clear();
    Mariadb mariadb(noPwdDef);
    // 无密码配置构造成功即可
}

// ============================================================================
// 7. 集成测试（需要真实的 MariaDB 服务运行）
//
// 运行方式：
//   ./test_tool_mariadb --gtest_also_run_disabled_tests --gtest_filter="MariadbIntegrationTest*"
//
// 前提条件：
//   1. 系统已安装并启动 MariaDB 服务
//   2. 管理员账户可正常连接
// ============================================================================

class MariadbIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override { def_ = MakeTestMariadbDef(); }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(TEST_TMP_BASE, ec);
    }

    MariadbDef def_;
};

TEST_F(MariadbIntegrationTest, DISABLED_GetVersion) {
    Mariadb mariadb(def_);
    auto result = mariadb.GetVersion();
    ASSERT_TRUE(result.IsDefalutSuccess()) << "GetVersion: " << result.msg;
    EXPECT_FALSE(result.msg.empty());
}

TEST_F(MariadbIntegrationTest, DISABLED_CreateAndDeleteUser) {
    Mariadb mariadb(def_);

    // 创建用户
    auto result = mariadb.CreateUser("scm_test_user", "ScmTest@2024!");
    ASSERT_TRUE(result.IsDefalutSuccess()) << "CreateUser: " << result.msg;

    // 删除用户及其数据库
    result = mariadb.DeleteUserAndDatabase("scm_test_user");
    EXPECT_TRUE(result.IsDefalutSuccess()) << "DeleteUserAndDatabase: " << result.msg;
}

TEST_F(MariadbIntegrationTest, DISABLED_ExecuteSqlFile) {
    std::string sqlDir = TEST_TMP_BASE + "/sql";
    fs::create_directories(sqlDir);
    std::string sqlFile = sqlDir + "/create_db.sql";
    CreateFileWithContent(sqlFile, "CREATE DATABASE IF NOT EXISTS scm_test_db DEFAULT CHARACTER SET utf8mb4;");

    Mariadb mariadb(def_);
    auto result = mariadb.ExecuteSqlFile(sqlFile);
    EXPECT_TRUE(result.IsDefalutSuccess()) << "ExecuteSqlFile: " << result.msg;
}

TEST_F(MariadbIntegrationTest, DISABLED_ExecuteSqlFileByUser) {
    Mariadb mariadb(def_);

    // 先创建测试用户
    auto result = mariadb.CreateUser("scm_file_user", "ScmFile@2024!");
    ASSERT_TRUE(result.IsDefalutSuccess()) << "CreateUser: " << result.msg;

    // 创建 SQL 文件
    std::string sqlDir = TEST_TMP_BASE + "/sql";
    fs::create_directories(sqlDir);
    std::string sqlFile = sqlDir + "/create_table.sql";
    CreateFileWithContent(sqlFile, "CREATE DATABASE IF NOT EXISTS scm_file_user_db DEFAULT CHARACTER SET utf8mb4;");

    // 以用户身份执行 SQL 文件
    result = mariadb.ExecuteSqlFileByUser("scm_file_user", "ScmFile@2024!", sqlFile);
    EXPECT_TRUE(result.IsDefalutSuccess()) << "ExecuteSqlFileByUser: " << result.msg;

    // 清理：删除用户及数据库
    result = mariadb.DeleteUserAndDatabase("scm_file_user");
    EXPECT_TRUE(result.IsDefalutSuccess()) << "DeleteUserAndDatabase: " << result.msg;
}

TEST_F(MariadbIntegrationTest, DISABLED_DeleteUserAndDatabase_WithUserDatabase) {
    Mariadb mariadb(def_);

    // 创建用户
    auto result = mariadb.CreateUser("scm_db_user", "ScmDb@2024!");
    ASSERT_TRUE(result.IsDefalutSuccess()) << "CreateUser: " << result.msg;

    // 创建以用户名为前缀的数据库
    std::string sqlDir = TEST_TMP_BASE + "/sql";
    fs::create_directories(sqlDir);
    std::string sqlFile = sqlDir + "/create_user_db.sql";
    CreateFileWithContent(sqlFile, "CREATE DATABASE IF NOT EXISTS scm_db_user_app DEFAULT CHARACTER SET utf8mb4;\n"
                                   "GRANT ALL PRIVILEGES ON scm_db_user_app.* TO 'scm_db_user'@'localhost';\n"
                                   "GRANT ALL PRIVILEGES ON scm_db_user_app.* TO 'scm_db_user'@'127.0.0.1';\n"
                                   "FLUSH PRIVILEGES;\n");
    result = mariadb.ExecuteSqlFile(sqlFile);
    ASSERT_TRUE(result.IsDefalutSuccess()) << "ExecuteSqlFile: " << result.msg;

    // 删除用户及其数据库
    result = mariadb.DeleteUserAndDatabase("scm_db_user");
    EXPECT_TRUE(result.IsDefalutSuccess()) << "DeleteUserAndDatabase: " << result.msg;
}
