/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/config.h"
#include <filesystem>
#include <gtest/gtest.h>

using namespace qifeng::scm;

// ============================================================================
// ConfigLoader 公开接口单元测试
// 覆盖 config.h 中声明的所有公开方法
// ============================================================================

// ---输出pwd当前进程的工作目录
TEST(TestBase, Pwd) {
    namespace fs = std::filesystem;
    std::string pwd = fs::current_path().string();
    EXPECT_TRUE(false) << "Pwd: " << pwd;
}

// ---------- GetAllServices() ----------
TEST(ConfigLoaderTest, GetAllServices) {
    ConfigLoader loader;
    auto result = loader.Initialize();
    EXPECT_TRUE(result.IsDefalutSuccess()) << "Initialize failed: code=" << result.code << " msg=" << result.msg;

    auto services = loader.GetAllServices();
    // 由用户设置输入验证
    EXPECT_TRUE(!services.empty()) << "GetAllServices failed: no services found";
}

// ---------- GetServiceByName (非 const 版本) ----------
TEST(ConfigLoaderTest, GetServiceByName) {
    ConfigLoader loader;
    // 初始化配置加载器
    loader.Initialize();
    ASSERT_TRUE(loader.IsInitialized()) << "Initialize failed: not initialized";
    // 获取服务
    auto* svc = loader.GetServiceByName("test_service_a");
    // 由用户设置输入验证
    EXPECT_TRUE(svc != nullptr) << "GetServiceByName failed: service not found";
}

// ---------- AddService() ----------
TEST(ConfigLoaderTest, AddService) {
    ConfigLoader loader;
    // 初始化配置加载器
    loader.Initialize();
    ASSERT_TRUE(loader.IsInitialized()) << "Initialize failed: not initialized";
    // 添加服务
    auto result = loader.AddService("./addServices");
    // 由用户设置输入验证
    EXPECT_TRUE(result.IsDefalutSuccess()) << "AddService failed: code=" << result.code << " msg=" << result.msg;
}

// ---------- ReloadService() ----------
TEST(ConfigLoaderTest, ReloadService) {
    ConfigLoader loader;
    // 初始化配置加载器
    loader.Initialize();
    ASSERT_TRUE(loader.IsInitialized()) << "Initialize failed: not initialized";
    // 重新加载服务
    auto result = loader.ReloadService("test_service_a");
    // 由用户设置输入验证
    EXPECT_TRUE(result.IsDefalutSuccess()) << "ReloadService failed: code=" << result.code << " msg=" << result.msg;
}

// ---------- RemoveService() ----------
TEST(ConfigLoaderTest, RemoveService) {
    ConfigLoader loader;
    // 初始化配置加载器
    loader.Initialize();
    ASSERT_TRUE(loader.IsInitialized()) << "Initialize failed: not initialized";
    // 移除服务
    auto result = loader.RemoveService("test_service_a");
    // 由用户设置输入验证
    EXPECT_TRUE(result.IsDefalutSuccess()) << "RemoveService failed: code=" << result.code << " msg=" << result.msg;
}

// ---------- UpgradeService() ----------
TEST(ConfigLoaderTest, UpgradeService) {
    ConfigLoader loader;
    // 初始化配置加载器
    loader.Initialize();
    ASSERT_TRUE(loader.IsInitialized()) << "Initialize failed: not initialized";
    // 升级服务
    auto result = loader.UpgradeService("./addServices");
    // 由用户设置输入验证
    EXPECT_TRUE(result.IsDefalutSuccess()) << "UpgradeService failed: code=" << result.code << " msg=" << result.msg;
}

// ---------- WriteSelfConfigFile() ----------
TEST(ConfigLoaderTest, WriteSelfConfigFile) {
    ConfigLoader loader;
    auto result = loader.WriteSelfConfigFile();
    // 由用户设置输入验证
    EXPECT_TRUE(result.IsDefalutSuccess())
        << "WriteSelfConfigFile failed: code=" << result.code << " msg=" << result.msg;
}
