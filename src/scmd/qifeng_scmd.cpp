/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/config.h"
#include "common/version.hpp"
#include "scmd/scmd_server.h"
#include "scmd/service_ctl.h"

#include <atomic>
#include <csignal>
#include <iostream>
#include <memory>

namespace {
    // 全局原子标志，用于信号处理器通知主循环退出
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    std::atomic<bool> gShouldExit{false};

    // 全局ScmServer指针，用于信号处理器中调用Stop
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    qifeng::scm::ScmServer* gServer{nullptr};

    /**
     * @brief 信号处理函数
     * @param signo 信号编号
     */
    void SignalHandler(int signo) {
        std::cout << "\n[scmd] Received signal " << signo << ", shutting down gracefully..." << std::endl;
        gShouldExit = true;
        if (gServer) {
            gServer->Stop();
        }
    }

    /**
     * @brief 注册信号处理器
     */
    void RegisterSignalHandlers() {
        struct sigaction sa {};
        sa.sa_handler = SignalHandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;

        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGINT, &sa, nullptr);
    }
}  // namespace

int main() {
    const auto& versionInfo = qifeng::scm::GetVersionInfo();
    std::cout << "qifeng_scmd version " << versionInfo.version
              << " (build: " << versionInfo.buildTime
              << ", commit: " << versionInfo.gitCommit << ")" << std::endl;

    // 1. 创建ServiceControl并初始化
    auto serviceControl = std::make_shared<qifeng::scm::ServiceControl>();
    auto result = serviceControl->Init();
    if (!result.IsDefalutSuccess()) {
        std::cerr << "[scmd] 初始化失败: " << result.msg << std::endl;
        return 1;
    }

    // 2. 获取配置信息
    const auto& configLoader = serviceControl->GetConfigLoader();
    const auto& configInfo = configLoader.GetConfigInfo();
    std::string socketPath = configInfo.udsSocketPath;

    // 3. 注册信号处理器
    RegisterSignalHandlers();

    // 4. 创建ScmServer并启动
    qifeng::scm::ScmServer server(serviceControl);
    gServer = &server;

    result = server.Start(socketPath);

    // 5. 清理
    gServer = nullptr;

    if (!result.IsDefalutSuccess()) {
        std::cerr << "[scmd] 服务异常退出: " << result.msg << std::endl;
        return 1;
    }

    std::cout << "[scmd] 服务已正常退出" << std::endl;
    return 0;
}
