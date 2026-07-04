/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/scmd_server.h"

#include "checker/checker_runner.h"
#include "common/config.h"
#include "common/types.h"
#include "ipc/data_def.h"
#include "ipc/protocol.h"
#include "ipc/uds.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/handlers/check_handler.h"
#include "scmd/handlers/info_handler.h"
#include "scmd/handlers/install_handler.h"
#include "scmd/handlers/kill_handler.h"
#include "scmd/handlers/list_handler.h"
#include "scmd/handlers/log_handler.h"
#include "scmd/handlers/reload_all_handler.h"
#include "scmd/handlers/reload_handler.h"
#include "scmd/handlers/restart_all_handler.h"
#include "scmd/handlers/restart_handler.h"
#include "scmd/handlers/slog_handler.h"
#include "scmd/handlers/start_handler.h"
#include "scmd/handlers/stop_handler.h"
#include "scmd/handlers/uninstall_handler.h"
#include "scmd/handlers/upgrade_handler.h"
#include "scmd/handlers/version_handler.h"
#include "scmd/service_ctl.h"

#include <cerrno>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace qifeng::scm {

    ScmServer::ScmServer(std::shared_ptr<ServiceControl> serviceControl) : mServiceControl(std::move(serviceControl)) {
    }

    ScmServer::~ScmServer() {
        Stop();
    }

    // NOLINTNEXTLINE(readability-function-size)
    ResultMsg ScmServer::Start(const std::string &socketPath) {
        if (!mServiceControl) {
            return MakeError("ServiceControl is null");
        }

        // 设置关键操作记录器文件路径
        const auto &configLoader = mServiceControl->GetConfigLoader();
        mKeyRecorder.SetFilePath(configLoader.GetKeyOptFilePath());

        // 恢复上次未完成的关键操作
        RecoverLastOperation();

        // 注册所有命令处理器
        RegisterHandlers();

        // 创建并初始化UDS服务端
        const auto &configInfo = configLoader.GetConfigInfo();
        mUdsServer = std::make_unique<UdsWrapper>(UdsMode::SERVER, socketPath, 1, configInfo.udsSocketMode);
        ResultMsg ret = mUdsServer->Initialize();
        if (ret.code != 0) {
            SLOG_ERROR << "Failed to initialize UDS server at " << socketPath << ", error: " << ret.msg;
            return MakeError(ret.msg);
        }

        SLOG_INFO << "ScmServer started, listening on " << socketPath;
        std::cout << "[scmd] 服务已启动，监听: " << socketPath << std::endl;

        mRunning = true;

        // 主事件循环：Accept -> Handle -> Repeat
        while (mRunning) {
            int clientFd = mUdsServer->AcceptClient();
            if (clientFd < 0) {
                if (!mRunning) {
                    break;
                }
                SLOG_WARN << "AcceptClient failed, continuing...";
                continue;
            }

            // 处理客户端请求（一问一答模式）
            HandleClient(clientFd);

            // 关闭客户端连接
            close(clientFd);
        }

        // 事件循环退出后安全释放UDS服务端
        mUdsServer.reset();

        SLOG_INFO << "ScmServer stopped";
        return MakeSuccess();
    }

    void ScmServer::Stop() {
        if (!mRunning) {
            return;
        }

        mRunning = false;
        SLOG_INFO << "ScmServer stopping...";

        // 关闭UDS服务端的监听socket，使AcceptClient因EBADF返回-1
        // 不释放mUdsServer对象本身，避免HandleClient中使用时对象已析构
        if (mUdsServer) {
            mUdsServer->Close();
        }
    }

    bool ScmServer::IsRunning() const {
        return mRunning;
    }

    bool ScmServer::RunSelfCheck() {
        // 从配置中获取自检开关和路径
        const auto &configLoader = mServiceControl->GetConfigLoader();
        const auto &configInfo = configLoader.GetConfigInfo();

        // 检查是否启用开机自检
        if (!configInfo.selftestEnabled) {
            SLOG_INFO << "Startup self-check is disabled by configuration";
            std::cout << "[scmd] 开机自检已禁用" << std::endl;
            return true;
        }

        // 确定自检配置路径
        if (!configInfo.selftestConfigPath.empty()) {
            mSelfTestConfigPath = configInfo.selftestConfigPath;
        } else {
            mSelfTestConfigPath = configInfo.configDir + "/selftest.json";
        }

        SLOG_INFO << "Running startup self-check with config: " << mSelfTestConfigPath;

        // 加载 selftest.json 到 mJsonLoader（由 ScmServer 管理，供 CheckerRunner 使用）
        // 文件加载失败时 JsonLoad 内部保留空根对象，CheckerRunner 会使用各 checker 的默认配置
        if (mJsonLoader.LoadFromFile(mSelfTestConfigPath)) {
            SLOG_INFO << "selftest json loaded into mJsonLoader";
        } else {
            SLOG_WARN << "selftest json load failed, fallback to built-in defaults";
        }

        auto report = CheckerRunner::Run(mJsonLoader);

        // 输出自检结果摘要到控制台
        std::cout << "[scmd] 开机自检完成: " << report.overallStatus << " (" << report.summary << ")" << std::endl;

        if (!report.overallOk) {
            SLOG_WARN << "Startup self-check FAILED: " << report.summary;

            // 根据配置决定自检失败后的行为
            if (configInfo.selftestFailAction == "halt") {
                SLOG_ERROR << "Self-check fail action is 'halt', aborting startup";
                std::cout << "[scmd] 自检失败且 fail_action=halt，服务将停止" << std::endl;
                return false;
            }
            // 默认行为：仅告警，不阻止服务启动
        }

        return true;
    }

    void ScmServer::RegisterHandlers() {
        // KILL 命令需要停止服务器自身，通过回调注入停止逻辑，避免 handler 反向依赖 ScmServer
        auto shutdownCallback = [this]() {
            mRunning = false;
            if (mUdsServer) {
                mUdsServer->Close();
            }
        };

        mDispatcher.Register(std::make_unique<VersionHandler>());
        mDispatcher.Register(std::make_unique<InstallHandler>());
        mDispatcher.Register(std::make_unique<StartHandler>());
        mDispatcher.Register(std::make_unique<StopHandler>());
        mDispatcher.Register(std::make_unique<RestartHandler>());
        mDispatcher.Register(std::make_unique<RestartAllHandler>());
        mDispatcher.Register(std::make_unique<UpgradeHandler>());
        mDispatcher.Register(std::make_unique<ListHandler>());
        mDispatcher.Register(std::make_unique<InfoHandler>());
        mDispatcher.Register(std::make_unique<LogHandler>());
        mDispatcher.Register(std::make_unique<SlogHandler>());
        mDispatcher.Register(std::make_unique<UninstallHandler>());
        mDispatcher.Register(std::make_unique<ReloadHandler>());
        mDispatcher.Register(std::make_unique<ReloadAllHandler>());
        mDispatcher.Register(std::make_unique<CheckHandler>(mSelfTestConfigPath));
        mDispatcher.Register(std::make_unique<KillHandler>(std::move(shutdownCallback)));

        SLOG_INFO << "All command handlers registered";
    }

    void ScmServer::HandleClient(int clientFd) {
        std::string buffer;
        constexpr size_t bufSize = 4096;
        std::vector<char> recvBuf(bufSize);

        // 接收请求数据（直接使用系统调用，不依赖mUdsServer对象生命周期）
        while (true) {
            ssize_t bytesRead = recv(clientFd, recvBuf.data(), bufSize, 0);
            if (bytesRead < 0) {
                if (errno == EINTR) {
                    continue;
                }
                SLOG_WARN << "Receive failed from client fd=" << clientFd;
                return;
            }
            if (bytesRead == 0) {
                // 客户端关闭连接
                break;
            }
            buffer.append(recvBuf.data(), static_cast<size_t>(bytesRead));

            // 尝试从缓冲区提取完整消息
            auto messages = ControlProtocol::ExtractMessages(buffer);
            if (!messages.empty()) {
                // 处理第一条完整消息
                ScmRequest request;
                if (!ControlProtocol::DecodeRequest(messages[0], request)) {
                    SLOG_WARN << "Failed to decode request from client";
                    ScmResponse errResp;
                    errResp.code = -1;
                    errResp.message = "Invalid request format";
                    auto encoded = ControlProtocol::EncodeResponse(errResp);
                    send(clientFd, encoded.data(), encoded.size(), 0);
                    return;
                }

                SLOG_INFO << "Received command: " << ScmCommandToString(request.Command());

                // 分发请求并获取响应
                ScmResponse response = mDispatcher.Dispatch(request, *mServiceControl, mKeyRecorder);

                // 发送响应（直接使用系统调用）
                auto encoded = ControlProtocol::EncodeResponse(response);
                send(clientFd, encoded.data(), encoded.size(), 0);

                SLOG_INFO << "Response sent: code=" << response.code;
                return;
            }
        }
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    void ScmServer::RecoverLastOperation() {
        KeyOperationRecord record;

        if (!mKeyRecorder.LoadLastOperation(record)) {
            return;
        }

        SLOG_INFO << "Found last key operation: " << record.optName << " service=" << record.serviceName
                  << " tarDir=" << record.tarDir << " sqlDir=" << record.sqlDir << " result=" << record.result;

        // result语义：0成功 1失败 2进行中（被异常终止）
        if (record.result == 0) {
            // 上次操作成功完成，直接清除记录
            SLOG_INFO << "Last operation completed successfully: " << record.optName;
            mKeyRecorder.Clear();
            return;
        }

        if (record.result == 1) {
            // 上次操作已执行结束但失败，只做提示不做自动恢复或清理
            SLOG_WARN << "Last operation failed: " << record.optName << " for " << record.serviceName;
            std::cout << "[scmd] 上次操作失败: " << record.optName << " 服务: " << record.serviceName
                      << "，请手动检查后重新执行" << std::endl;
            return;
        }

        // result == 2：上次操作进行中被异常终止，尝试恢复
        SLOG_INFO << "Recovering incomplete operation: " << record.optName << " for " << record.serviceName;
        std::cout << "[scmd] 检测到未完成的操作: " << record.optName << " 服务: " << record.serviceName
                  << "，正在恢复..." << std::endl;

        // 将操作名字符串映射为命令枚举，统一走 switch-case 处理
        auto cmdOpt = StringToScmCommand(record.optName);
        if (!cmdOpt.has_value()) {
            SLOG_WARN << "Unknown operation to recover: " << record.optName;
            std::cout << "[scmd] 操作恢复失败: " << record.optName << " - 未知操作类型" << std::endl;
            return;
        }

        ResultMsg recoverResult;
        switch (cmdOpt.value()) {
            case ScmCommand::INSTALL: {
                // 安装未完成，检查服务是否已存在
                const auto &configLoader = mServiceControl->GetConfigLoader();
                auto* svc = configLoader.GetServiceByName(record.serviceName);
                if (svc) {
                    // 服务已部分安装，先卸载清理再重新安装
                    SLOG_INFO << "Service partially installed, cleaning up: " << record.serviceName;
                    auto cleanResult = mServiceControl->UninstallService(record.serviceName);
                    if (!cleanResult.IsDefalutSuccess()) {
                        recoverResult = cleanResult;
                    } else if (!record.tarDir.empty()) {
                        // 有软件包路径，可以重新安装
                        SLOG_INFO << "Retrying install with tarDir: " << record.tarDir;
                        recoverResult = mServiceControl->Installed(record.serviceName, record.tarDir);
                    } else {
                        recoverResult = MakeWarning("Install interrupted but tarDir not recorded, cannot retry");
                    }
                } else if (!record.tarDir.empty()) {
                    // 服务不存在且有软件包路径，直接重新安装
                    SLOG_INFO << "Retrying install with tarDir: " << record.tarDir;
                    recoverResult = mServiceControl->Installed(record.tarDir, record.serviceName);
                } else {
                    recoverResult = MakeWarning("Install interrupted but tarDir not recorded, cannot retry");
                }
                break;
            }

            case ScmCommand::UPGRADE: {
                // 升级未完成，检查服务状态
                const auto &configLoader = mServiceControl->GetConfigLoader();
                auto* svc = configLoader.GetServiceByName(record.serviceName);
                if (svc && !record.tarDir.empty()) {
                    // 服务存在且有新版本路径，尝试重新升级
                    SLOG_INFO << "Retrying upgrade with tarDir: " << record.tarDir;
                    recoverResult = mServiceControl->UpgradeService(record.serviceName, record.tarDir);
                } else {
                    SLOG_INFO << "Upgrade was interrupted, manual check recommended for: " << record.serviceName;
                    recoverResult =
                        MakeWarning("Upgrade was interrupted, manual check recommended for: " + record.serviceName);
                }
                break;
            }

            case ScmCommand::UNINSTALL: {
                // 卸载未完成，尝试继续卸载
                const auto &configLoader = mServiceControl->GetConfigLoader();
                auto* svc = configLoader.GetServiceByName(record.serviceName);
                if (svc) {
                    SLOG_INFO << "Uninstall was interrupted, retrying: " << record.serviceName;
                    recoverResult = mServiceControl->UninstallService(record.serviceName);
                } else {
                    recoverResult = MakeSuccess();
                }
                break;
            }

            case ScmCommand::START: {
                SLOG_INFO << "Start was interrupted, retrying: " << record.serviceName;
                recoverResult = mServiceControl->StartService(record.serviceName);
                break;
            }

            case ScmCommand::STOP: {
                SLOG_INFO << "Stop was interrupted, retrying: " << record.serviceName;
                recoverResult = mServiceControl->StopService(record.serviceName);
                break;
            }

            case ScmCommand::VERSION:
            case ScmCommand::RESTART:
            case ScmCommand::RESTART_ALL:
            case ScmCommand::LIST:
            case ScmCommand::INFO:
            case ScmCommand::LOG:
            case ScmCommand::RELOAD:
            case ScmCommand::RELOAD_ALL:
            case ScmCommand::KILL:
            case ScmCommand::SLOG:
            case ScmCommand::CHECK: {
                // 这些命令不支持自动恢复，记录警告并保留原始记录
                SLOG_WARN << "Operation does not support auto-recovery: " << record.optName;
                recoverResult = MakeWarning("Operation does not support auto-recovery: " + record.optName);
                break;
            }

            default: {
                // 无法识别的操作类型，记录警告并保留原始记录
                SLOG_WARN << "Unknown operation to recover: " << record.optName;
                recoverResult = MakeWarning("Unknown operation: " + record.optName);
                break;
            }
        }

        if (recoverResult.IsDefalutSuccess()) {
            SLOG_INFO << "Recovery completed successfully for: " << record.optName;
            std::cout << "[scmd] 操作恢复成功: " << record.optName << std::endl;
            // 恢复成功后标记为已完成(result=0)，不立即清除记录
            // 这样即使本次恢复后再次崩溃，下次启动看到result=0会知道已恢复
            mKeyRecorder.UpdateResult(0);
        } else {
            SLOG_WARN << "Recovery failed for " << record.optName << ": " << recoverResult.msg;
            std::cout << "[scmd] 操作恢复失败: " << record.optName << " - " << recoverResult.msg << std::endl;
            // 恢复失败时保留记录(result=2)，以便下次启动再次尝试恢复
        }
    }

}  // namespace qifeng::scm
