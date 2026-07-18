/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/scmd_server.h"

#include "common/config.h"
#include "common/types.h"
#include "ipc/data_def.h"
#include "ipc/protocol.h"
#include "ipc/uds.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/handler_registry.h"
#include "scmd/operation_recovery.h"
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

        // 构造 HandlerContext 并从注册表加载所有 handler（替代手工 RegisterHandlers）
        // KILL 命令需要停止服务器自身，通过回调注入停止逻辑，避免 handler 反向依赖 ScmServer
        // CheckHandler 需要自检配置路径，作为运行期依赖传入
        const auto &configInfo = configLoader.GetConfigInfo();
        std::string selfTestConfigPath = configInfo.selftestConfigPath.empty()
            ? configInfo.configDir + "/selftest.json"
            : configInfo.selftestConfigPath;
        HandlerContext ctx;
        ctx.selfTestConfigPath = selfTestConfigPath;
        ctx.shutdownCallback = [this]() {
            mRunning = false;
            if (mUdsServer) {
                mUdsServer->Close();
            }
        };
        mDispatcher.LoadFromRegistry(ctx);

        // 恢复上次未完成的关键操作（需在 handler 注册后，恢复通过分发器查表执行）
        // 由独立的 OperationRecoveryService 承担，ScmServer 只负责构造和触发
        mRecoveryService = std::make_unique<OperationRecoveryService>(
            mDispatcher, mServiceControl->GetServiceContext(), mKeyRecorder);
        mRecoveryService->Run();

        // 创建并初始化UDS服务端（configInfo 已在上方 HandlerContext 构造时获取）
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
                ScmResponse response = mDispatcher.Dispatch(request, mServiceControl->GetServiceContext(), mKeyRecorder);

                // 发送响应（直接使用系统调用）
                auto encoded = ControlProtocol::EncodeResponse(response);
                send(clientFd, encoded.data(), encoded.size(), 0);

                SLOG_INFO << "Response sent: code=" << response.code;
                return;
            }
        }
    }

}  // namespace qifeng::scm
