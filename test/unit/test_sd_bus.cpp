/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#include "service_manger/dbus_manager.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace qifeng::scm;  // NOLINT(google-build-using-namespace)

namespace {
    // 系统中确定存在的运行中服务（用于只读测试）
    constexpr const char* RunningService = "cron";
    // 测试服务名称（需要root权限部署后用于读写测试）
    constexpr const char* TestService = "testa";

    // 等待服务状态变更的轮询间隔和最大次数
    constexpr int PollIntervalMs = 200;
    constexpr int MaxPollCount = 50;

    // 轮询等待服务达到指定活跃状态
    // 当单元未加载（如刚停止的服务）时，视为过渡状态继续等待
    void WaitForActiveState(DBusManager &mgr, const std::string &serviceName, const std::string &expectedState) {
        for (int i = 0; i < MaxPollCount; ++i) {
            auto result = mgr.GetUnitActiveState(serviceName);
            if (result.IsDefalutSuccess() && result.msg == expectedState) {
                return;
            }
            // 查询失败（单元未加载等）也视为过渡状态，继续轮询
            std::this_thread::sleep_for(std::chrono::milliseconds(PollIntervalMs));
        }
    }
}  // namespace

// ============================================================================
// 第一部分：只读测试（无需root权限，使用系统总线查询已有系统服务）
// 覆盖：构造函数、IsConnected、析构函数、GetUnitActiveState、
//        GetUnitSubState、GetServiceMainPID、错误处理
// ============================================================================

// ---------- 构造函数（系统总线）+ IsConnected() ----------
TEST(DBusManagerReadOnly, ConstructorSystem) {
    DBusManager mgr;
    EXPECT_TRUE(mgr.IsConnected()) << "System bus connection should succeed";
}

// ---------- 构造函数（用户总线）+ IsConnected() ----------
TEST(DBusManagerReadOnly, ConstructorUser) {
    DBusManager mgr(BusType::User);
    // 用户总线在无桌面会话时可能连接失败，此处仅验证不崩溃
    // 如果连接成功则 IsConnected 返回 true
    if (mgr.IsConnected()) {
        SUCCEED() << "User bus connected";
    } else {
        GTEST_SKIP() << "User bus not available (no desktop session), skipping";
    }
}

// ---------- 析构函数 ----------
TEST(DBusManagerReadOnly, Destructor) {
    {
        DBusManager mgr;
        EXPECT_TRUE(mgr.IsConnected());
    }
    // 析构后不应崩溃，验证 RAII 释放正常
    SUCCEED();
}

// ---------- GetUnitActiveState() — 查询运行中的系统服务 ----------
TEST(DBusManagerReadOnly, GetUnitActiveStateRunning) {
    DBusManager mgr;
    ASSERT_TRUE(mgr.IsConnected());

    auto result = mgr.GetUnitActiveState(RunningService);
    EXPECT_TRUE(result.IsDefalutSuccess()) << "GetUnitActiveState failed: " << result.msg;
    EXPECT_EQ(result.msg, "active") << "Expected 'active', got: " << result.msg;
}

// ---------- GetUnitSubState() — 查询运行中的系统服务 ----------
TEST(DBusManagerReadOnly, GetUnitSubStateRunning) {
    DBusManager mgr;
    ASSERT_TRUE(mgr.IsConnected());

    auto result = mgr.GetUnitSubState(RunningService);
    EXPECT_TRUE(result.IsDefalutSuccess()) << "GetUnitSubState failed: " << result.msg;
    EXPECT_EQ(result.msg, "running") << "Expected 'running', got: " << result.msg;
}

// ---------- GetServiceMainPID() — 查询运行中的系统服务 ----------
TEST(DBusManagerReadOnly, GetServiceMainPID) {
    DBusManager mgr;
    ASSERT_TRUE(mgr.IsConnected());

    auto result = mgr.GetServiceMainPID(RunningService);
    EXPECT_TRUE(result.IsDefalutSuccess()) << "GetServiceMainPID failed: " << result.msg;
    int pid = std::stoi(result.msg);
    EXPECT_GT(pid, 0) << "MainPID should be > 0 for running service, got: " << result.msg;
}

// ---------- 查询不存在的服务状态 ----------
// LoadUnit对不存在的服务也会创建存根单元，返回"inactive"而非报错
// 这与systemd行为一致：不存在的服务等同于未加载的inactive状态
TEST(DBusManagerReadOnly, GetStateNonexistentUnit) {
    DBusManager mgr;
    ASSERT_TRUE(mgr.IsConnected());

    auto result = mgr.GetUnitActiveState("nonexistent_service_xyz");
    EXPECT_TRUE(result.IsDefalutSuccess()) << "GetUnitActiveState should succeed (stub unit)";
    EXPECT_EQ(result.msg, "inactive") << "Nonexistent service should be 'inactive', got: " << result.msg;
}

// ---------- 查询不存在的服务PID ----------
// LoadUnit对不存在的服务创建存根单元，MainPID为0
TEST(DBusManagerReadOnly, GetPIDNonexistentUnit) {
    DBusManager mgr;
    ASSERT_TRUE(mgr.IsConnected());

    auto result = mgr.GetServiceMainPID("nonexistent_service_xyz");
    EXPECT_TRUE(result.IsDefalutSuccess()) << "GetServiceMainPID should succeed (stub unit)";
    EXPECT_EQ(result.msg, "0") << "Nonexistent service MainPID should be 0, got: " << result.msg;
}

// ============================================================================
// 第二部分：读写测试（需要root权限，使用testa测试服务）
// 运行方式：sudo ./build/bin/test_sd_bus --gtest_filter=DBusManagerReadWrite.*
//
// 前置条件：
//   1. 编译测试服务：已由 CMake 自动编译 testa
//   2. 部署测试服务：
//      sudo cp build/test/unit/test_service/testa /usr/local/bin/
//      sudo cp build/test/unit/test_service/testa.service /etc/systemd/system/
//      sudo systemctl daemon-reload
// ============================================================================

// ---------- StartUnit() ----------
TEST(DBusManagerReadWrite, StartUnit) {
    DBusManager mgr;
    ASSERT_TRUE(mgr.IsConnected());

    // 先确保服务处于停止状态
    mgr.StopUnit(TestService);
    WaitForActiveState(mgr, TestService, "inactive");

    auto result = mgr.StartUnit(TestService);
    EXPECT_TRUE(result.IsDefalutSuccess()) << "StartUnit failed: " << result.msg;

    // 等待服务启动完成
    WaitForActiveState(mgr, TestService, "active");
}

// ---------- StopUnit() ----------
TEST(DBusManagerReadWrite, StopUnit) {
    DBusManager mgr;
    ASSERT_TRUE(mgr.IsConnected());

    // 先确保服务处于运行状态
    mgr.StartUnit(TestService);
    WaitForActiveState(mgr, TestService, "active");

    auto result = mgr.StopUnit(TestService);
    EXPECT_TRUE(result.IsDefalutSuccess()) << "StopUnit failed: " << result.msg;

    // 等待服务停止完成
    WaitForActiveState(mgr, TestService, "inactive");
}

// ---------- RestartUnit() ----------
TEST(DBusManagerReadWrite, RestartUnit) {
    DBusManager mgr;
    ASSERT_TRUE(mgr.IsConnected());

    // 先确保服务处于运行状态
    mgr.StartUnit(TestService);
    WaitForActiveState(mgr, TestService, "active");

    auto result = mgr.RestartUnit(TestService);
    EXPECT_TRUE(result.IsDefalutSuccess()) << "RestartUnit failed: " << result.msg;

    // 等待服务重启完成
    WaitForActiveState(mgr, TestService, "active");
}

// ---------- GetUnitActiveState() — 运行中 ----------
TEST(DBusManagerReadWrite, GetUnitActiveStateRunning) {
    DBusManager mgr;
    ASSERT_TRUE(mgr.IsConnected());

    // 确保服务运行
    mgr.StartUnit(TestService);
    WaitForActiveState(mgr, TestService, "active");

    auto result = mgr.GetUnitActiveState(TestService);
    EXPECT_TRUE(result.IsDefalutSuccess()) << "GetUnitActiveState failed: " << result.msg;
    EXPECT_EQ(result.msg, "active") << "Expected 'active', got: " << result.msg;
}

// ---------- GetUnitActiveState() — 已停止 ----------
TEST(DBusManagerReadWrite, GetUnitActiveStateStopped) {
    DBusManager mgr;
    ASSERT_TRUE(mgr.IsConnected());

    // 确保服务停止
    mgr.StopUnit(TestService);
    WaitForActiveState(mgr, TestService, "inactive");

    auto result = mgr.GetUnitActiveState(TestService);
    EXPECT_TRUE(result.IsDefalutSuccess()) << "GetUnitActiveState failed: " << result.msg;
    EXPECT_EQ(result.msg, "inactive") << "Expected 'inactive', got: " << result.msg;
}

// ---------- GetUnitSubState() — 运行中 ----------
TEST(DBusManagerReadWrite, GetUnitSubStateRunning) {
    DBusManager mgr;
    ASSERT_TRUE(mgr.IsConnected());

    // 确保服务运行
    mgr.StartUnit(TestService);
    WaitForActiveState(mgr, TestService, "active");

    auto result = mgr.GetUnitSubState(TestService);
    EXPECT_TRUE(result.IsDefalutSuccess()) << "GetUnitSubState failed: " << result.msg;
    EXPECT_EQ(result.msg, "running") << "Expected 'running', got: " << result.msg;
}

// ---------- GetUnitSubState() — 已停止 ----------
TEST(DBusManagerReadWrite, GetUnitSubStateStopped) {
    DBusManager mgr;
    ASSERT_TRUE(mgr.IsConnected());

    // 确保服务停止
    mgr.StopUnit(TestService);
    WaitForActiveState(mgr, TestService, "inactive");

    auto result = mgr.GetUnitSubState(TestService);
    EXPECT_TRUE(result.IsDefalutSuccess()) << "GetUnitSubState failed: " << result.msg;
    EXPECT_EQ(result.msg, "dead") << "Expected 'dead', got: " << result.msg;
}

// ---------- GetServiceMainPID() — 运行中的测试服务 ----------
TEST(DBusManagerReadWrite, GetServiceMainPID) {
    DBusManager mgr;
    ASSERT_TRUE(mgr.IsConnected());

    // 确保服务运行
    mgr.StartUnit(TestService);
    WaitForActiveState(mgr, TestService, "active");

    auto result = mgr.GetServiceMainPID(TestService);
    EXPECT_TRUE(result.IsDefalutSuccess()) << "GetServiceMainPID failed: " << result.msg;
    int pid = std::stoi(result.msg);
    EXPECT_GT(pid, 0) << "MainPID should be > 0 for running service, got: " << result.msg;
}

// ---------- EnableUnit() ----------
TEST(DBusManagerReadWrite, EnableUnit) {
    DBusManager mgr;
    ASSERT_TRUE(mgr.IsConnected());

    auto result = mgr.EnableUnit(TestService);
    EXPECT_TRUE(result.IsDefalutSuccess()) << "EnableUnit failed: " << result.msg;
}

// ---------- DisableUnit() ----------
TEST(DBusManagerReadWrite, DisableUnit) {
    DBusManager mgr;
    ASSERT_TRUE(mgr.IsConnected());

    auto result = mgr.DisableUnit(TestService);
    EXPECT_TRUE(result.IsDefalutSuccess()) << "DisableUnit failed: " << result.msg;
}

// ---------- ReloadDaemon() ----------
TEST(DBusManagerReadWrite, ReloadDaemon) {
    DBusManager mgr;
    ASSERT_TRUE(mgr.IsConnected());

    auto result = mgr.ReloadDaemon();
    EXPECT_TRUE(result.IsDefalutSuccess()) << "ReloadDaemon failed: " << result.msg;
}

// ---------- 启动不存在的服务 — 错误处理 ----------
TEST(DBusManagerReadWrite, StartNonexistentUnit) {
    DBusManager mgr;
    ASSERT_TRUE(mgr.IsConnected());

    auto result = mgr.StartUnit("nonexistent_service_xyz");
    EXPECT_FALSE(result.IsDefalutSuccess()) << "StartUnit should fail for nonexistent service";
}

// ---------- 测试清理：停止并禁用测试服务 ----------
TEST(DBusManagerReadWrite, Cleanup) {
    DBusManager mgr;
    ASSERT_TRUE(mgr.IsConnected());

    // 测试结束后停止服务，清理环境
    mgr.StopUnit(TestService);
    mgr.DisableUnit(TestService);
    SUCCEED();
}
