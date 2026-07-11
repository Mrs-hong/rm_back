/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief INIT_NGINX 命令处理器
     * @details 独立配置服务的 nginx，安装前端配置到系统 nginx 目录。
     */
    class InitNginxHandler : public ICommandHandler {
    public:
        explicit InitNginxHandler(const HandlerContext&) {}

        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           ServiceControl& serviceControl,
                           KeyOperationRecorder& recorder) override;
    };

}  // namespace qifeng::scm
