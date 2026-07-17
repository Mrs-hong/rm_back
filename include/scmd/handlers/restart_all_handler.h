/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief RESTART_ALL 命令处理器
     * @details 重启所有已安装服务。
     */
    class RestartAllHandler : public ICommandHandler {
    public:
        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           ServiceControl& serviceControl,
                           KeyOperationRecorder& recorder) override;
    };

}  // namespace qifeng::scm
