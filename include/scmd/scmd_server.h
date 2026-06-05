/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include "common/types.h"
#include "ipc/data_def.h"
#include "service_manger/key_recoder.h"

#include <atomic>
#include <memory>
#include <string>

namespace qifeng::scm {
    class ServiceControl;
    class UdsWrapper;

    /**
     * @brief SCMD 服务端
     * @details 居于uds功能块，负责接收scmctl的请求，并根据请求调用ServiceControl的功能，将结果返回；
     * 其它要求：
     *    1. 一问一答，暂时是同步的（单客户端）；
     *    2. 支持优雅退出、当执行完相应的操作后退出
     *    3. 补充关键操作记录到文件last_key_opt.yaml、用于启动时恢复
     */
    class ScmServer {
    public:
        /**
         * @brief 构造函数
         * @param serviceControl 服务控制门面类
         */
        explicit ScmServer(std::shared_ptr<ServiceControl> serviceControl);

        ~ScmServer();

        ScmServer(const ScmServer &) = delete;
        ScmServer &operator=(const ScmServer &) = delete;
        ScmServer(ScmServer &&) = delete;
        ScmServer &operator=(ScmServer &&) = delete;

        /**
         * @brief 启动服务端（初始化UDS、进入事件循环）
         * @param socketPath UDS socket文件路径
         * @return ResultMsg 启动结果
         */
        ResultMsg Start(const std::string &socketPath);

        /**
         * @brief 停止服务端（优雅退出）
         */
        void Stop();

        /**
         * @brief 检查是否正在运行
         * @return 是否正在运行
         */
        bool IsRunning() const;

    private:
        /**
         * @brief 处理单个客户端连接（一问一答模式）
         * @param clientFd 客户端文件描述符
         */
        void HandleClient(int clientFd);

        /**
         * @brief 根据请求分发到ServiceControl对应方法
         * @param request 请求结构体
         * @return ScmResponse 响应结构体
         */
        ScmResponse DispatchRequest(const ScmRequest &request);

        /**
         * @brief 恢复上次未完成的关键操作
         */
        void RecoverLastOperation();

        std::shared_ptr<ServiceControl> mServiceControl;
        std::unique_ptr<UdsWrapper> mUdsServer;
        KeyOperationRecorder mKeyRecorder;
        std::atomic<bool> mRunning {false};
    };
}  // namespace qifeng::scm
