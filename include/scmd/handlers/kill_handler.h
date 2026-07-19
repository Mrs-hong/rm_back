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
     * @par 与 STOP 命令（无 -n 参数）的区别
     *          - `scmc kill`：直接置 ScmServer 内部 mRunning=false，scmd 主循环优雅退出。
     *            不经过 systemd，进程退出后由 systemd 根据 Restart 策略决定是否拉起。
     *            适合"希望 scmd 立即停止接收新请求并完成手头任务"的场景。
     *          - `scmc stop`（无参数）：经 systemd DBus StopUnit 停止 qifeng-scmd.service 单元。
     *            由 systemd 主动停止，若配置 Restart=always 会被立即拉起新实例。
     *            适合"希望通过 systemd 正常停止单元"的场景。
     * @see StopHandler
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
