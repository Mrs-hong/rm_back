/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief START 命令处理器
     * @details 行为分两种路径：
     *          - 携带 `-n <service>`：启动指定服务，并在操作前后记录关键操作日志用于断点恢复；
     *          - 无 `-n` 参数：操作 scmd 自身，调用 ServiceManager::StartScmdSelf。
     *            由于 scmd 正在处理请求即说明自身已运行，此路径直接返回成功，
     *            不会主动拉起已停止的 scmd 进程（需通过 systemctl start qifeng-scmd.service）。
     */
    class StartHandler : public ICommandHandler {
    public:
        explicit StartHandler(const HandlerContext&) {}
        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           const ServiceContext& ctx,
                           KeyOperationRecorder& recorder) override;
        ResultMsg Recover(const KeyOperationRecord& record, const ServiceContext& ctx) override;
    };

}  // namespace qifeng::scm
