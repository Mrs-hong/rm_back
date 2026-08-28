/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <common/logger.h>
#include <iostream>

#include "qifeng_framework/common/config_manager.h"
#include "qifeng_framework/common/utils/time.h"
#include "qifeng_framework/dao/db_pool.h"
#include "qifeng_framework/http/http_register.h"
#include "workflow/WFTaskFactory.h"
#include "workflow/Workflow.h"

#include "common/config/database_config.h"
#include "common/config/path_config.h"
#include "common/service_readiness.h"
#include "common/timer_manager.h"
#include "common/utils/symlink_manager.h"
#include "core/device/device_collect_task.h"
#include "core/system/tasks/cleanup_task.h"
#include "core/upgrade/upgrade_realtime.h"
#include "dao_managers/casbin_dao_manager.h"
#include "internal/aas/lifecycle.h"
#include "internal/display_refresh_task.h"
#include "internal/hal/hal_bridge.h"
#include "internal/hal/lifecycle.h"
#include "internal/lms/lifecycle.h"
#include "internal/recording_manager.h"
#include "schedule/pcm/pcm_engine.h"

static bool InitInternal() {
    bool ret = true;
    auto startTime = GetTimeMs();
    auto endTime = GetTimeMs();

    startTime = GetTimeMs();
    ret = qifeng_ca::aas::InitAas();
    if (!ret) {
        SLOG_ERROR << "AAS 初始化失败";
        return ret;
    }
    endTime = GetTimeMs();
    SLOG_INFO << "AAS 初始化耗时: " << (endTime - startTime) << "ms";

    startTime = GetTimeMs();
    ret = qifeng_ca::hal::InitHal();
    if (!ret) {
        SLOG_ERROR << "HAL 初始化失败";
        return ret;
    }
    endTime = GetTimeMs();
    SLOG_INFO << "HAL 初始化耗时: " << (endTime - startTime) << "ms";

    // LMS初始化耗时在分钟级, 改为异步初始化避免阻塞启动流程
    SLOG_INFO << "main: LMS 异步初始化W启动";
    startTime = GetTimeMs();
    auto* lmsTask = WFTaskFactory::create_go_task("LmsInit", [startTime]() {
        bool ok = qifeng_ca::lms::InitLms();
        auto finishTime = GetTimeMs();
        if (!ok) {
            SLOG_ERROR << "LMS 异步初始化失败, 耗时: " << (finishTime - startTime) << "ms";
            return;
        }
        SLOG_INFO << "LMS 异步初始化完成, 耗时: " << (finishTime - startTime) << "ms";
    });
    auto* series = Workflow::create_series_work(lmsTask, nullptr);
    series->start();

    return true;
}

static void ShutdownInternal() {
    qifeng_ca::hal::ShutdownHal();
    qifeng_ca::aas::ShutdownAas();
    qifeng_ca::lms::ShutdownLms();
}

static void RunHttp() {
    auto &svcCfg = qifeng_ca::PathConfig::GetInstance();

    // 初始化鉴权
    qifeng_ca::CasbinDaoManager::GetInstance().Initialize(svcCfg.GetCasbinPath());
    SLOG_DEBUG << "Bms: CasbinDaoManager initialized end";

    // 创建并配置drogon应用
    drogon::app().loadConfigFile(svcCfg.GetServicePath());
    SLOG_DEBUG << "Bms: drogon app initialized end";

    qifeng::HttpRegistry::Instance().RegisterAll();

    qifeng_ca::ServiceReadiness::GetInstance().MarkReady();
    // 启动drogon服务器
    FLOG_INFO("Drogon HTTP server starting...");
    drogon::app().run();
}

[[maybe_unused]] static void DoGracefulShutdown() {
    SLOG_INFO << "main: graceful shutdown starting...";

    // 停止录音(如果正在进行)
    auto &recMgr = qifeng_ca::RecordingManager::GetInstance();
    if (recMgr.IsRecording()) {
        qifeng_ca::HalBridge::GetInstance().StopRecording();
        recMgr.StopRecording(recMgr.GetActiveAudioId());
        SLOG_INFO << "main: recording stopped";
    }

    // 停止定时任务
    qifeng_ca::DeviceCollectTask::GetInstance().Stop();
    SLOG_INFO << "main: device collect task stopped";

    // 停止显示屏定时刷新任务
    qifeng_ca::DisplayRefreshTask::GetInstance().Stop();
    SLOG_INFO << "main: display refresh task stopped";

    // 停止数据清理定时任务
    qifeng_ca::CleanupTask::GetInstance().Stop();
    SLOG_INFO << "main: cleanup task stopped";

    // 停止 OTA 升级探测
    qifeng_ca::UpgradeRealtimeManager::GetInstance().Stop();
    SLOG_INFO << "main: ota upgrade manager stopped";

    // 取消所有命名定时器(防止回调中访问已释放资源导致core)
    qifeng_ca::TimerManager::GetInstance().CancelAll();
    SLOG_INFO << "main: all named timers canceled";

    // 停止任务
    qifeng_ca::PcmEngine::GetInstance().Stop();

    // 释放其他服务资源
    ShutdownInternal();

    SLOG_INFO << "main: graceful shutdown completed";
}

static int StartBms() {
    SLOG_DEBUG << "Bms: initialized start";
    auto startTime = GetTimeMs();

    // 启动PCM任务处理线程
    if (!qifeng_ca::PcmEngine::GetInstance().Init()) {
        SLOG_ERROR << "Bms: PcmEngine init failed";
        return -1;
    }
    qifeng_ca::PcmEngine::GetInstance().Run();

    // 注册所有默认定时器名称
    qifeng_ca::TimerManager::GetInstance().DefaultRegisterAll();

    // 启动定时任务
    qifeng_ca::DeviceCollectTask::GetInstance().Start();

    // 启动数据清理定时任务(1小时周期)
    qifeng_ca::CleanupTask::GetInstance().Start();

    // 启动显示屏定时刷新任务(1s周期, 录音中跳过)
    qifeng_ca::DisplayRefreshTask::GetInstance().Start();

    // 启动 OTA 升级探测(网络/USB, 按开关独立启停)
    qifeng_ca::UpgradeRealtimeManager::GetInstance().Start();

    SLOG_DEBUG << "Bms: initialized end, BMS模块耗时: " << (GetTimeMs() - startTime) << "ms";
    // 启动http监听
    RunHttp();

    return 0;
}

[[nodiscard]] bool InitDbPool() {
    DBPoolConfig dbPoolConfig;
    // 只配置用户名和密码、其余使用连接池中的默认值
    dbPoolConfig.user = qifeng_ca::DatabaseConfig::GetInstance().GetMysqlUser();
    dbPoolConfig.password = qifeng_ca::DatabaseConfig::GetInstance().GetMysqlPassword();
    return DBPool::GetInstance().Initialize(dbPoolConfig);
}

int main() {
    auto &config = ConfigManager::GetInstance();

    // 初始化配置管理器
    if (!config.Initialize("qifeng_ca", "config/config.yaml")) {
        FLOG_ERROR("Failed to initialize config manager");
        return -1;
    }

    SLOG_DEBUG << "initialized config end";

    // 初始化 /data2 软连接管理(检测磁盘、创建软连接)
    qifeng_ca::SymlinkManager::GetInstance().Init();
    SLOG_DEBUG << "initialized symlink end";

    if (!InitDbPool()) {
        SLOG_ERROR << "Failed to initialize database connection pool";
        return -1;
    }
    SLOG_DEBUG << "initialized DBPool end";

    if (!InitInternal()) {
        return -2;
    }
    SLOG_DEBUG << "initialized hal and aas end";

    int ret = StartBms();

    return ret;
}
