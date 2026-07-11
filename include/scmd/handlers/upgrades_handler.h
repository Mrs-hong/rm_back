/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief UPGRADES 命令处理器
     * @details 使用服务内部预置升级包执行升级，并在操作前后记录关键操作日志。
     *          流程：从服务 upgrade.soft_dir 查找升级包 → 解析版本 → 升级 → 写入结果文件。
     */
    class UpgradesHandler : public ICommandHandler {
    public:
        explicit UpgradesHandler(const HandlerContext&) {}

        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           ServiceControl& serviceControl,
                           KeyOperationRecorder& recorder) override;

        ResultMsg Recover(const KeyOperationRecord& record, ServiceControl& serviceControl) override;
    };

}  // namespace qifeng::scm
