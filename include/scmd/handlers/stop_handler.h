/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief STOP 命令处理器
     * @details 停止指定服务，并在操作前后记录关键操作日志。
     */
    class StopHandler : public ICommandHandler {
    public:
        explicit StopHandler(const HandlerContext&) {}

        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           ServiceControl& serviceControl,
                           KeyOperationRecorder& recorder) override;

        ResultMsg Recover(const KeyOperationRecord& record, ServiceControl& serviceControl) override;
    };

}  // namespace qifeng::scm
