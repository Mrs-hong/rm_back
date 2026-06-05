/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/scmd_server.h"

#include "common/config.h"
#include "common/version.hpp"
#include "ipc/data_def.h"
#include "ipc/protocol.h"
#include "ipc/uds.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/service_ctl.h"

#include <cerrno>
#include <iostream>
#include <json/json.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace qifeng::scm {

    ScmServer::ScmServer(std::shared_ptr<ServiceControl> serviceControl) : mServiceControl(std::move(serviceControl)) {
    }

    ScmServer::~ScmServer() {
        Stop();
    }

    ResultMsg ScmServer::Start(const std::string &socketPath) {
        if (!mServiceControl) {
            return MakeError("ServiceControl is null");
        }

        // 设置关键操作记录器文件路径
        const auto &configLoader = mServiceControl->GetConfigLoader();
        mKeyRecorder.SetFilePath(configLoader.GetKeyOptFilePath());

        // 恢复上次未完成的关键操作
        RecoverLastOperation();

        // 创建并初始化UDS服务端
        const auto &configInfo = configLoader.GetConfigInfo();
        mUdsServer = std::make_unique<UdsWrapper>(UdsMode::SERVER, socketPath, 1, configInfo.udsSocketMode);
        if (!mUdsServer->Initialize()) {
            return MakeError("Failed to initialize UDS server at " + socketPath);
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

                SLOG_INFO << "Received command: " << ScmCommandToString(request.command)
                          << " serviceName=" << request.serviceName;

                // 分发请求并获取响应
                ScmResponse response = DispatchRequest(request);

                // 发送响应（直接使用系统调用）
                auto encoded = ControlProtocol::EncodeResponse(response);
                send(clientFd, encoded.data(), encoded.size(), 0);

                SLOG_INFO << "Response sent: code=" << response.code;
                return;
            }
        }
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ScmResponse ScmServer::DispatchRequest(const ScmRequest &request) {
        ScmResponse response;

        switch (request.command) {
            case ScmCommand::VERSION: {
                const auto &versionInfo = GetVersionInfo();
                response.code = 0;
                response.message = "qifeng_scm version " + versionInfo.version;
                response.data["version"] = versionInfo.version;
                response.data["buildTime"] = versionInfo.buildTime;
                response.data["gitCommit"] = versionInfo.gitCommit;
                break;
            }

            case ScmCommand::INSTALL: {
                // 记录关键操作（操作前，result=2表示进行中），包含软件包路径
                mKeyRecorder.RecordOperation({"install", request.serviceName, 2, request.tarDir, ""});
                auto result = mServiceControl->Installed(request.serviceName, request.tarDir);
                response.code = result.code;
                response.message = result.msg;
                mKeyRecorder.UpdateResult(result.IsDefalutSuccess() ? 0 : 1);
                if (result.IsDefalutSuccess()) {
                    mKeyRecorder.Clear();
                }
                break;
            }

            case ScmCommand::START: {
                mKeyRecorder.RecordOperation({"start", request.serviceName, 2, "", ""});
                auto result = mServiceControl->StartService(request.serviceName);
                response.code = result.code;
                response.message = result.msg;
                mKeyRecorder.UpdateResult(result.IsDefalutSuccess() ? 0 : 1);
                if (result.IsDefalutSuccess()) {
                    mKeyRecorder.Clear();
                }
                break;
            }

            case ScmCommand::STOP: {
                mKeyRecorder.RecordOperation({"stop", request.serviceName, 2, "", ""});
                auto result = mServiceControl->StopService(request.serviceName);
                response.code = result.code;
                response.message = result.msg;
                mKeyRecorder.UpdateResult(result.IsDefalutSuccess() ? 0 : 1);
                if (result.IsDefalutSuccess()) {
                    mKeyRecorder.Clear();
                }
                break;
            }

            case ScmCommand::RESTART: {
                auto result = mServiceControl->RestartService(request.serviceName);
                response.code = result.code;
                response.message = result.msg;
                break;
            }

            case ScmCommand::RESTART_ALL: {
                auto result = mServiceControl->RestartAllServices();
                response.code = result.code;
                response.message = result.msg;
                break;
            }

            case ScmCommand::RELOAD: {
                auto result = mServiceControl->ReloadService(request.serviceName);
                response.code = result.code;
                response.message = result.msg;
                break;
            }

            case ScmCommand::UPGRADE: {
                // 记录关键操作，包含新版本软件包路径
                mKeyRecorder.RecordOperation({"upgrade", request.serviceName, 2, request.tarDir, ""});
                auto result = mServiceControl->UpgradeService(request.serviceName, request.tarDir);
                response.code = result.code;
                response.message = result.msg;
                mKeyRecorder.UpdateResult(result.IsDefalutSuccess() ? 0 : 1);
                if (result.IsDefalutSuccess()) {
                    mKeyRecorder.Clear();
                }
                break;
            }

            case ScmCommand::LIST: {
                // 直接构建服务列表JSON数据到response.data中
                auto allServices = mServiceControl->GetConfigLoader().GetAllServices();
                Json::Value servicesArray(Json::arrayValue);
                for (const auto &svc : allServices) {
                    Json::Value svcJson;
                    svcJson["serviceName"] = svc.serviceName;
                    svcJson["version"] = svc.version;
                    svcJson["isAutoStart"] = svc.isAutoStart;
                    servicesArray.append(svcJson);
                }
                response.code = 0;
                response.message = allServices.empty() ? "No services installed" : "success";
                response.data = servicesArray;
                break;
            }

            case ScmCommand::INFO: {
                auto result = mServiceControl->GetServiceStatus(request.serviceName);
                response.code = result.code;
                if (result.IsDefalutSuccess()) {
                    response.message = "success";
                    auto info = mServiceControl->GetServiceRuntimeInfo(request.serviceName);
                    if (info.pid > 0) {
                        Json::Value root;
                        root["serviceName"] = request.serviceName;
                        root["version"] = info.currentVersion;
                        root["pid"] = static_cast<int>(info.pid);
                        root["status"] = info.status;
                        root["startTime"] = info.startTime;
                        root["runTime"] = info.runTime;
                        root["memoryUsage"] = static_cast<Json::UInt64>(info.memoryUsage);
                        root["cpuUsage"] = static_cast<Json::UInt64>(info.cpuUsage);
                        root["configFilePath"] = info.configFilePath;
                        root["logFilePath"] = info.logFilePath;
                        root["dbFilePath"] = info.dbFilePath;
                        root["recoveryCount"] = info.recoveryCount;
                        response.data = std::move(root);
                        break;
                    }
                } else {
                    response.message = result.msg;
                }
                break;
            }

            case ScmCommand::LOG: {
                auto result = mServiceControl->GetOperationLog(request.logLevel, request.logCount);
                response.code = result.code;
                response.message = result.msg;
                break;
            }

            case ScmCommand::UNINSTALL: {
                mKeyRecorder.RecordOperation({"uninstall", request.serviceName, 2, "", ""});
                auto result = mServiceControl->UninstallService(request.serviceName);
                response.code = result.code;
                response.message = result.msg;
                mKeyRecorder.UpdateResult(result.IsDefalutSuccess() ? 0 : 1);
                if (result.IsDefalutSuccess()) {
                    mKeyRecorder.Clear();
                }
                break;
            }

            case ScmCommand::KILL: {
                SLOG_INFO << "Received KILL command, initiating graceful shutdown";
                response.code = 0;
                response.message = "scmd is shutting down gracefully";
                // 延迟停止，确保响应先发送回客户端
                mRunning = false;
                if (mUdsServer) {
                    mUdsServer->Close();
                }
                break;
            }

            default: {
                response.code = -1;
                response.message = "Unknown command";
                break;
            }
        }

        return response;
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
        if (record.result == 2) {
            // 上次操作进行中被异常终止，尝试恢复
            SLOG_INFO << "Recovering incomplete operation: " << record.optName << " for " << record.serviceName;
            std::cout << "[scmd] 检测到未完成的操作: " << record.optName << " 服务: " << record.serviceName
                      << "，正在恢复..." << std::endl;

            ResultMsg recoverResult;
            if (record.optName == "install") {
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
            } else if (record.optName == "upgrade") {
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
            } else if (record.optName == "uninstall") {
                // 卸载未完成，尝试继续卸载
                const auto &configLoader = mServiceControl->GetConfigLoader();
                auto* svc = configLoader.GetServiceByName(record.serviceName);
                if (svc) {
                    SLOG_INFO << "Uninstall was interrupted, retrying: " << record.serviceName;
                    recoverResult = mServiceControl->UninstallService(record.serviceName);
                } else {
                    recoverResult = MakeSuccess();
                }
            } else if (record.optName == "start") {
                SLOG_INFO << "Start was interrupted, retrying: " << record.serviceName;
                recoverResult = mServiceControl->StartService(record.serviceName);
            } else if (record.optName == "stop") {
                SLOG_INFO << "Stop was interrupted, retrying: " << record.serviceName;
                recoverResult = mServiceControl->StopService(record.serviceName);
            } else {
                SLOG_WARN << "Unknown operation to recover: " << record.optName;
                recoverResult = MakeWarning("Unknown operation: " + record.optName);
            }

            if (recoverResult.IsDefalutSuccess()) {
                SLOG_INFO << "Recovery completed successfully for: " << record.optName;
                std::cout << "[scmd] 操作恢复成功: " << record.optName << std::endl;
            } else {
                SLOG_WARN << "Recovery failed for " << record.optName << ": " << recoverResult.msg;
                std::cout << "[scmd] 操作恢复失败: " << record.optName << " - " << recoverResult.msg << std::endl;
            }
        } else if (record.result == 1) {
            // 上次操作失败，记录日志不做自动恢复
            SLOG_WARN << "Last operation failed: " << record.optName << " for " << record.serviceName;
            std::cout << "[scmd] 上次操作失败: " << record.optName << " 服务: " << record.serviceName << "，请手动检查"
                      << std::endl;
        } else {
            // result==0 表示上次操作成功完成，无需恢复
            SLOG_INFO << "Last operation completed successfully: " << record.optName;
        }

        // 恢复完成后清除记录
        mKeyRecorder.Clear();
    }

}  // namespace qifeng::scm
