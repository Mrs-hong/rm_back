/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/scmd_types.h"
#include "common/version.hpp"
#include "scmd/service_ctl.h"

#include <chrono>
#include <iostream>
#include <thread>

using namespace qifeng::scm;  // NOLINT

namespace {
    constexpr const char* TestSoftTarPath = "/home/lyh/code/cppProgram/t_mariadb.tar.gz";
    constexpr const char* TestServiceName = "mariadb";
    constexpr const char* TestSqlDir = "/home/lyh/code/cppProgram/sql";

    // 等待数据库端口就绪
    bool WaitForDatabaseReady(ServiceMain &svcMain, const std::string &serviceName, int timeoutSec) {
        for (int i = 0; i < timeoutSec * 2; ++i) {
            auto result = svcMain.ExecuteServiceSQL(serviceName, "SELECT 1", "");
            if (result.IsDefalutSuccess()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        return false;
    }

    // 打印分隔线
    void PrintStep(const std::string &step) {
        std::cout << "\n========== " << step << " ==========" << std::endl;
    }

    // 检查结果并打印
    bool CheckResult(const ResultMsg &result, const std::string &action) {
        if (result.IsDefalutSuccess()) {
            std::cout << "[OK] " << action << std::endl;
            if (!result.msg.empty()) {
                std::cout << "     详情: " << result.msg << std::endl;
            }
            return true;
        }
        std::cerr << "[FAIL] " << action << ", 错误: " << result.msg << std::endl;
        return false;
    }
}  // namespace

int main(void) {
    std::cout << "qifeng_scm version: " << GetVersionInfo().version << std::endl;
    std::cout << "build time: " << GetVersionInfo().buildTime << std::endl;
    std::cout << "git commit: " << GetVersionInfo().gitCommit << std::endl;

    ServiceMain serviceMain;

    // === 步骤1: 初始化（首次启动，无已安装服务） ===
    PrintStep("步骤1: 初始化ServiceMain");
    ResultMsg result = serviceMain.Init();
    if (!CheckResult(result, "Init")) {
        std::cerr << "初始化失败，退出" << std::endl;
        return -1;
    }

    // === 步骤2: 安装mariadb服务 ===
    PrintStep("步骤2: 安装mariadb服务");
    result = serviceMain.Installed(TestSoftTarPath, TestServiceName);
    if (!CheckResult(result, "Installed mariadb")) {
        std::cerr << "安装失败，退出" << std::endl;
        return -1;
    }

    // === 步骤3: 初始化数据库（创建管理员用户、执行SQL初始化脚本） ===
    PrintStep("步骤3: 初始化数据库");
    result = serviceMain.InitServiceDataBase(TestServiceName, TestSqlDir);
    if (!CheckResult(result, "InitServiceDataBase")) {
        std::cerr << "数据库初始化失败，退出" << std::endl;
        return -1;
    }

    // === 步骤4: 通过systemd启动mariadb服务 ===
    PrintStep("步骤4: 启动mariadb服务");
    result = serviceMain.StartService(TestServiceName);
    if (!CheckResult(result, "StartService")) {
        std::cerr << "启动服务失败，退出" << std::endl;
        return -1;
    }

    // 等待数据库就绪
    std::cout << "等待数据库就绪..." << std::endl;
    if (!WaitForDatabaseReady(serviceMain, TestServiceName, 30)) {
        std::cerr << "[FAIL] 数据库启动超时" << std::endl;
        return -1;
    }
    std::cout << "[OK] 数据库已就绪" << std::endl;

    // === 步骤5: 创建新用户 ===
    PrintStep("步骤5: 创建数据库用户");
    result = serviceMain.CreateDatabaseUser(TestServiceName, "testuser", "Test@123");
    CheckResult(result, "CreateDatabaseUser: testuser");

    // === 步骤6: 创建数据库 ===
    PrintStep("步骤6: 创建数据库");
    result = serviceMain.CreateServiceDatabase(TestServiceName, "testdb");
    CheckResult(result, "CreateServiceDatabase: testdb");

    // === 步骤7: 创建表 ===
    PrintStep("步骤7: 创建表");
    result = serviceMain.CreateDatabaseTable(TestServiceName, "testdb", "users",
                                             "id INT PRIMARY KEY AUTO_INCREMENT, "
                                             "name VARCHAR(100) NOT NULL, "
                                             "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP");
    CheckResult(result, "CreateDatabaseTable: users");

    // === 步骤8: 插入测试数据 ===
    PrintStep("步骤8: 插入测试数据");
    result =
        serviceMain.ExecuteServiceSQL(TestServiceName, "INSERT INTO users (name) VALUES ('alice'), ('bob')", "testdb");
    CheckResult(result, "ExecuteServiceSQL: INSERT");

    // === 步骤9: 查询验证 ===
    PrintStep("步骤9: 查询验证");
    result = serviceMain.ExecuteServiceSQL(TestServiceName, "SELECT * FROM users", "testdb");
    CheckResult(result, "ExecuteServiceSQL: SELECT");

    // === 步骤10: 获取运行中服务状态 ===
    PrintStep("步骤10: 获取服务状态（运行中）");
    result = serviceMain.GetServiceStatus(TestServiceName);
    CheckResult(result, "GetServiceStatus (running)");

    // === 步骤11: 停止服务 ===
    PrintStep("步骤11: 停止mariadb服务");
    result = serviceMain.StopService(TestServiceName);
    CheckResult(result, "StopService");

    // === 步骤12: 获取停止后服务状态 ===
    PrintStep("步骤12: 获取服务状态（已停止）");
    result = serviceMain.GetServiceStatus(TestServiceName);
    CheckResult(result, "GetServiceStatus (stopped)");

    PrintStep("测试完成");
    std::cout << "\n所有测试步骤已执行完毕。" << std::endl;
    std::cout << "请检查以下目录确认文件生成：" << std::endl;
    std::cout << "  .logs/    - 日志文件" << std::endl;
    std::cout << "  .services/ - 服务目录和systemd服务文件" << std::endl;
    std::cout << "  .services/.init/scmd_mariadb.service - systemd单元文件" << std::endl;

    return 0;
}
