/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief RESTART 命令处理器
     * @details 行为分两种路径：
     *          - 携带 `-n <service>`：重启指定单个服务；
     *          - 无 `-n` 参数：操作 scmd 自身，调用 ServiceManager::RestartScmdSelf，
     *            经 systemd DBus RestartUnit("qifeng-scmd.service") 重启自身守护进程。
     *            重启过程中当前 UDS 连接会断开，客户端应在收到响应后等待若干秒再重连。
     * @par 权限要求
     *          scmd 进程需具备 systemd DBus RestartUnit 权限（通常通过 polkit 策略授予）。
     */
    class RestartHandler : public ICommandHandler {
    public:
        explicit RestartHandler(const HandlerContext&) {}
        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           const ServiceContext& ctx,
                           KeyOperationRecorder& recorder) override;
    };

}  // namespace qifeng::scm
