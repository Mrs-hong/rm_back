/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

#include <string>

namespace qifeng::scm {

    /**
     * @brief CHECK 命令处理器
     * @details 手动触发设备自检，调用 CheckerRunner 执行检查并返回结果。
     */
    class CheckHandler : public ICommandHandler {
    public:
        /**
         * @brief 构造函数
         * @param ctx 运行期依赖上下文，从中提取 selfTestConfigPath
         */
        explicit CheckHandler(const HandlerContext& ctx) : mConfigPath(ctx.selfTestConfigPath) {}

        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           const ServiceContext& ctx,
                           KeyOperationRecorder& recorder) override;

    private:
        std::string mConfigPath;
    };

}  // namespace qifeng::scm
