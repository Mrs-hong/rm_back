/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

/**
 * 基于 gtest 测试 MariaDB 数据库服务的安装、初始化、启动、创建用户、创建表、停止服务
 *
 * 测试策略：
 * - 测试从项目根目录启动，配置文件 .config/scmd.yaml 自然生效
 * - 测试用例只负责安装和使用，不执行卸载操作
 * - 所有文件保留在项目目录中，供运行后查看
 * - TearDown 仅停止进程，不删除任何文件
 *
 * 运行方式：
 *   cd /home/lyh/code/cppProgram/qifeng-scm && ./build/bin/test_mariadb
 * root 运行 systemd 测试：
 *   cd /home/lyh/code/cppProgram/qifeng-scm && sudo ./build/bin/test_mariadb
 */

#include <gtest/gtest.h>

#include <common/config.h>
#include <common/scmd_def.h>
#include <common/scmd_types.h>
#include <common/utils.h>
#include <dbinit/database_init.h>
#include <qifeng_framework/common/logger.h>
#include <service_manger/dbus_manager.h>
#include <service_manger/service_manager.h>

#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

using qifeng::scm::BusType;
using qifeng::scm::ConfigLoader;
using qifeng::scm::DatabaseConfig;
using qifeng::scm::DatabaseInit;
using qifeng::scm::DBusManager;
using qifeng::scm::ServiceManager;

class MariaDBServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 初始化 Logger，使 SLOG_ 宏的日志能写入 .logs 目录
        Logger::GetInstance().Initialize("./.logs", "scmd_test", 52428800, 7);

        configLoader_ = std::make_shared<ConfigLoader>();
        auto result = configLoader_->Initialize();
        EXPECT_TRUE(result.IsDefalutSuccess() || result.code == 1) << result.msg;
        serviceManager_ = std::make_unique<ServiceManager>(configLoader_);
        // EXPECT_TRUE(serviceManager_->IsInitialized()) << "ServiceManager initialization failed";
    }

    void TearDown() override {
        if (serviceManager_) {
            // 仅停止服务进程，不卸载、不删除任何文件
            serviceManager_->StopService("mariadb");
        }
        std::string killCmd = "fuser -k 3306/tcp 2>/dev/null || true";
        int killRet = system(killCmd.c_str());
        (void)killRet;
    }

    // 准备 MariaDB 运行时所需的 libtinfo.so.5 软链接（系统只有 libtinfo.so.6）
    void PrepareServiceLibtinfo() {
        std::string svcLibDir = configLoader_->GetServiceRootDir("mariadb") + "/lib";
        std::string linkPath = svcLibDir + "/libtinfo.so.5";
        if (!fs::exists(linkPath) && !fs::exists("/usr/lib/x86_64-linux-gnu/libtinfo.so.5") &&
            fs::exists("/usr/lib/x86_64-linux-gnu/libtinfo.so.6")) {
            fs::create_directories(svcLibDir);
            fs::create_symlink("/usr/lib/x86_64-linux-gnu/libtinfo.so.6", linkPath);
        }
        std::string libPath = svcLibDir + ":/usr/lib/x86_64-linux-gnu";
        const char* existingLdPath = getenv("LD_LIBRARY_PATH");
        if (existingLdPath != nullptr) {
            libPath = libPath + ":" + existingLdPath;
        }
        setenv("LD_LIBRARY_PATH", libPath.c_str(), 1);
    }

    // 确保 mariadb 服务已安装：如果未安装则安装，已安装则复用
    void EnsureMariadbInstalled() {
        auto* svc = configLoader_->GetServiceByName("mariadb");
        if (svc == nullptr) {
            DoInstallMariadb();
        } else {
            EnsureServiceFilesReady();
        }
        PrepareServiceLibtinfo();
    }

    void DoInstallMariadb() {
        auto result = serviceManager_->InstallService("/home/lyh/code/cppProgram/smcd_mariadb.tar.gz", "mariadb");
        ASSERT_TRUE(result.IsDefalutSuccess()) << result.msg;
        auto* svc = configLoader_->GetServiceByName("mariadb");
        ASSERT_NE(svc, nullptr) << "Service should be registered after install";
    }

    // root 环境下如果服务已存在，重新生成 .service 文件和 systemd 软链接
    void EnsureServiceFilesReady() {
        if (HasSystemdAccess()) {
            auto result = serviceManager_->ReloadService("mariadb");
            EXPECT_TRUE(result.IsDefalutSuccess()) << result.msg;
        }
    }

    DatabaseConfig MakeDatabaseConfig() const {
        DatabaseConfig cfg;
        cfg.dbInfo.dbType = qifeng::scm::DatabaseType::MYSQL;
        cfg.binDir = configLoader_->GetServiceRootDir("mariadb");
        auto* svc = configLoader_->GetServiceByName("mariadb");
        if (svc) {
            cfg.dataDir = svc->execInfo.dataDir;
            cfg.port = svc->resourcesInfo.ports.empty() ? 0 : svc->resourcesInfo.ports[0];
            cfg.osUser = svc->execInfo.user;
            cfg.osGroup = svc->execInfo.user;
        }
        cfg.adminUser = "root";
        cfg.adminPwd = "";
        return cfg;
    }

    bool HasSystemdAccess() const {
        if (getuid() != 0) {
            return false;
        }
        DBusManager dbus(qifeng::scm::BusType::System);
        return dbus.IsConnected();
    }

    std::shared_ptr<ConfigLoader> configLoader_;
    std::unique_ptr<ServiceManager> serviceManager_;
};

// 测试用例 1：未安装服务时，启动应该失败
TEST_F(MariaDBServiceTest, StartWithoutInstall) {
    auto* svc = configLoader_->GetServiceByName("mariadb");
    if (svc != nullptr) {
        GTEST_SKIP() << "mariadb already installed from previous test, skipping";
    }
    auto result = serviceManager_->StartService("mariadb");
    EXPECT_FALSE(result.IsDefalutSuccess()) << "Should fail when service is not installed";
}

// 测试用例 2：首次安装 smcd_mariadb.tar.gz，验证数据库初始化成功
TEST_F(MariaDBServiceTest, InstallAndInitialize) {
    EnsureMariadbInstalled();
    auto* svc = configLoader_->GetServiceByName("mariadb");
    fs::path dataDir = svc->execInfo.dataDir;
    EXPECT_TRUE(fs::exists(dataDir / "mysql")) << "MySQL system tables should be initialized";
    EXPECT_TRUE(fs::exists(dataDir / "ibdata1")) << "InnoDB system tablespace should exist";
    fs::path serviceFile = ".services/.init/scmd_mariadb.service";
    EXPECT_TRUE(fs::exists(serviceFile)) << "Systemd service file should be created in .init";
}

// 测试用例 3：安装后通过 systemd 启动（需要 root 权限）
TEST_F(MariaDBServiceTest, StartAfterInstall) {
    if (!HasSystemdAccess()) {
        GTEST_SKIP() << "Systemd DBus not accessible, skipping systemd start test";
    }
    EnsureMariadbInstalled();
    auto result = serviceManager_->StartService("mariadb");
    EXPECT_TRUE(result.IsDefalutSuccess()) << result.msg;
    bool portReady = false;
    for (int i = 0; i < 30; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        auto dbConfig = MakeDatabaseConfig();
        DatabaseInit dbInit(dbConfig);
        if (dbInit.IsRunning()) {
            portReady = true;
            break;
        }
    }
    EXPECT_TRUE(portReady) << "MariaDB should be listening on port 3306";
}

// 测试用例 4：数据库运行后，创建用户、数据库、表
TEST_F(MariaDBServiceTest, DatabaseOperations) {
    EnsureMariadbInstalled();
    auto dbConfig = MakeDatabaseConfig();
    DatabaseInit dbInit(dbConfig);
    if (HasSystemdAccess()) {
        auto result = serviceManager_->StartService("mariadb");
        ASSERT_TRUE(result.IsDefalutSuccess()) << result.msg;
    } else {
        auto result = dbInit.Start();
        ASSERT_TRUE(result.IsDefalutSuccess()) << result.msg;
    }
    bool ready = false;
    for (int i = 0; i < 30; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (dbInit.IsRunning()) {
            ready = true;
            break;
        }
    }
    ASSERT_TRUE(ready) << "Database should be running";
    auto result = dbInit.CreateUser("testuser", "Test@123");
    EXPECT_TRUE(result.IsDefalutSuccess()) << result.msg;
    result = dbInit.CreateDatabase("testdb");
    EXPECT_TRUE(result.IsDefalutSuccess()) << result.msg;
    result = dbInit.CreateTable("testdb", "users", "id INT PRIMARY KEY AUTO_INCREMENT, name VARCHAR(100)");
    EXPECT_TRUE(result.IsDefalutSuccess()) << result.msg;
    result = dbInit.ExecuteSQL("SELECT 1 FROM testdb.users LIMIT 1", "testdb");
    EXPECT_TRUE(result.IsDefalutSuccess()) << result.msg;
    if (HasSystemdAccess()) {
        result = serviceManager_->StopService("mariadb");
        EXPECT_TRUE(result.IsDefalutSuccess()) << result.msg;
    } else {
        result = dbInit.Stop();
        EXPECT_TRUE(result.IsDefalutSuccess()) << result.msg;
    }
}

// 测试用例 5：停止服务
TEST_F(MariaDBServiceTest, StopService) {
    if (!HasSystemdAccess()) {
        GTEST_SKIP() << "Systemd DBus not accessible, skipping systemd stop test";
    }
    EnsureMariadbInstalled();
    auto result = serviceManager_->StartService("mariadb");
    ASSERT_TRUE(result.IsDefalutSuccess()) << result.msg;
    result = serviceManager_->StopService("mariadb");
    EXPECT_TRUE(result.IsDefalutSuccess()) << result.msg;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    auto dbConfig = MakeDatabaseConfig();
    DatabaseInit dbInit(dbConfig);
    EXPECT_FALSE(dbInit.IsRunning()) << "MariaDB should be stopped";
}
