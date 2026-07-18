/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief SLOG 命令处理器
     * @details 返回指定服务的日志内容。读取顺序：
     *   1. 优先读取服务日志文件 <logsDir>/<serviceName>/<serviceName>.log（最后 N 行）
     *   2. 文件不存在或为空时回退读取 systemd journal
     *   serviceName 为空时读取 scmd 自身日志（<logsDir>/qifeng-scm/qifeng-scm.log），
     *   与 scmd.log（SLOG 内部日志）隔离。
     */
    class SlogHandler : public ICommandHandler {
    public:
        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           ServiceControl& serviceControl,
                           KeyOperationRecorder& recorder) override;
    };

}  // namespace qifeng::scm
