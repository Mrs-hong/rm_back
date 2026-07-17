/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief RESET_NGINX 命令处理器
     * @details 重置服务的 nginx 配置，删除系统 conf 和服务 nginx 目录。
     */
    class ResetNginxHandler : public ICommandHandler {
    public:
        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           ServiceControl& serviceControl,
                           KeyOperationRecorder& recorder) override;
    };

}  // namespace qifeng::scm
