/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief CLEAR_MODEL 命令处理器
     * @details 停用并备份模型：将 model_dir 下指定模型重命名为 <name>.back，
     *          验证依赖服务无影响后完成，否则回退。
     */
    class ClearModelHandler : public ICommandHandler {
    public:
        explicit ClearModelHandler(const HandlerContext&) {}
        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           const ServiceContext& ctx,
                           KeyOperationRecorder& recorder) override;
    };

}  // namespace qifeng::scm
