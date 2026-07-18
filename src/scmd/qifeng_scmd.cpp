/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/config.h"
#include "common/utils.h"
#include "common/version.hpp"
#include "scmd/scmd_server.h"
#include "scmd/self_check_service.h"
#include "scmd/service_ctl.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <memory>

namespace {
    // 全局原子标志，用于信号处理器通知主循环退出
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    std::atomic<bool> gShouldExit {false};

    // 全局ScmServer指针，用于信号处理器中调用Stop
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    qifeng::scm::ScmServer* gServer {nullptr};

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
    const auto &versionInfo = qifeng::scm::GetVersionInfo();
    std::cout << "qf_scmd version " << versionInfo.version << " (build: " << versionInfo.buildTime
              << ", commit: " << versionInfo.gitCommit << ")" << std::endl;

    // 1. 创建ServiceControl并初始化
    auto serviceControl = std::make_shared<qifeng::scm::ServiceControl>();
    auto result = serviceControl->Init();
    if (!result.IsDefalutSuccess()) {
        std::cerr << "[scmd] 初始化失败: " << result.msg << std::endl;
        return 1;
    }

    // 2. 获取配置信息
    const auto &configLoader = serviceControl->GetConfigLoader();
    const auto &configInfo = configLoader.GetConfigInfo();
    std::string socketPath = configInfo.udsSocketPath;

    // 新需求3.1：将 scmd 自身的 stdout/stderr 重定向到 <logsDir>/qifeng-scm/qifeng-scm.log
    // 与 scmd.log（SLOG 内部日志）隔离，专门存放 scmd 进程的 stdout/stderr 输出。
    // Init() 中 CreateServiceLogDirs() 已创建 qifeng-scm/ 目录；systemd 的
    // StandardOutput=append: 配置（见 qifeng-scmd.service）也会写入同一文件，
    // 此处 freopen 保证手动启动 scmd 时输出也能被记录。
    std::string scmdSelfLogFile =
        qifeng::scm::utils::JoinPath(qifeng::scm::utils::JoinPath(configInfo.logsDir, "qifeng-scm"), "qifeng-scm.log");
    if (freopen(scmdSelfLogFile.c_str(), "a", stdout) != nullptr) {
        setvbuf(stdout, nullptr, _IOLBF, 0);  // 行缓冲，确保输出及时落盘
    } else {
        std::cerr << "[scmd] Warning: failed to redirect stdout to " << scmdSelfLogFile << std::endl;
    }
    if (freopen(scmdSelfLogFile.c_str(), "a", stderr) != nullptr) {
        setvbuf(stderr, nullptr, _IOLBF, 0);
    }

    // 3. 注册信号处理器
    RegisterSignalHandlers();

    // 4. 创建ScmServer
    qifeng::scm::ScmServer server(serviceControl);
    gServer = &server;

    // 5. 执行开机自检（在 Start 之前，确保设备就绪后再进入服务循环）
    // 自检逻辑由独立的 SelfCheckService 承担，ScmServer 不再承担该职责
    qifeng::scm::SelfCheckService selfCheckService(configLoader);
    if (!selfCheckService.Run()) {
        std::cerr << "[scmd] 开机自检失败且 fail_action=halt，服务中止启动" << std::endl;
        gServer = nullptr;
        return 1;
    }

    // 6. 启动服务（进入UDS事件循环）
    result = server.Start(socketPath);

    // 7. 清理
    gServer = nullptr;

    if (!result.IsDefalutSuccess()) {
        std::cerr << "[scmd] 服务异常退出: " << result.msg << std::endl;
        return 1;
    }

    std::cout << "[scmd] 服务已正常退出" << std::endl;
    return 0;
}
