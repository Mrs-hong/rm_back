/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief UNINSTALL_ALL 命令处理器
     * @details 卸载所有已安装服务，但保留 scmd 自身。
     */
    class UninstallAllHandler : public ICommandHandler {
    public:
        explicit UninstallAllHandler(const HandlerContext&) {}

        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           ServiceControl& serviceControl,
                           KeyOperationRecorder& recorder) override;
    };

}  // namespace qifeng::scm
