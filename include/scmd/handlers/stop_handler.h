/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief STOP 命令处理器
     * @details 行为分两种路径：
     *          - 携带 `-n <service>`：停止指定服务，并在操作前后记录关键操作日志用于断点恢复；
     *          - 无 `-n` 参数：操作 scmd 自身，调用 ServiceManager::StopScmdSelf，
     *            经 systemd DBus StopUnit("qifeng-scmd.service") 停止自身守护进程。
     * @par 权限要求
     *          scmd 进程需具备 systemd DBus StopUnit 权限（通常通过 polkit 策略授予）。
     * @par 与 KILL 命令的区别
     *          - `scmc stop`（无参数）经 systemd 主动停止单元，若配置 Restart=always 会被立即拉起；
     *          - `scmc kill` 直接置 ScmServer mRunning=false 优雅退出，不经 systemd。
     * @see KillHandler
     */
    class StopHandler : public ICommandHandler {
    public:
        explicit StopHandler(const HandlerContext&) {}
        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           const ServiceContext& ctx,
                           KeyOperationRecorder& recorder) override;
        ResultMsg Recover(const KeyOperationRecord& record, const ServiceContext& ctx) override;
    };

}  // namespace qifeng::scm
