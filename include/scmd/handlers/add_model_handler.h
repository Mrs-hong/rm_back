/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief ADD_MODEL 命令处理器
     * @details 安装/升级模型文件：将指定目录或 tar 包中的模型安装到 scmd.yaml 配置的
     *          model_dir 下，自动停止依赖模型的服务并验证运行状态，失败时回退。
     */
    class AddModelHandler : public ICommandHandler {
    public:
        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           ServiceControl& serviceControl,
                           KeyOperationRecorder& recorder) override;
    };

}  // namespace qifeng::scm
