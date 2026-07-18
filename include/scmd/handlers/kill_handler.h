/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

#include <functional>

namespace qifeng::scm {

    /**
     * @brief KILL 命令处理器
     * @details 触发 scmd 优雅退出。通过回调将具体的停止逻辑委托给 ScmServer，
     *          避免 handler 反向依赖 ScmServer 内部状态。
     */
    class KillHandler : public ICommandHandler {
    public:
        /**
         * @brief 构造函数
         * @param ctx 运行期依赖上下文，从中提取 shutdownCallback
         */
        explicit KillHandler(const HandlerContext& ctx) : mShutdownCallback(ctx.shutdownCallback) {}

        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           const ServiceContext& ctx,
                           KeyOperationRecorder& recorder) override;

    private:
        std::function<void()> mShutdownCallback;
    };

}  // namespace qifeng::scm
