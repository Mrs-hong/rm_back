/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

#include <string>

namespace qifeng::scm {

    struct CheckRequest {
        std::string configPath;  // 自检配置文件路径（可选，为空则使用默认路径）
    };

    template <>
    struct RequestCommand<CheckRequest> { static constexpr ScmCommand value = ScmCommand::CHECK; };

    inline Json::Value ToJson(const CheckRequest& req) {
        Json::Value root(Json::objectValue);
        if (!req.configPath.empty()) {
            root["configPath"] = req.configPath;
        }
        return root;
    }

    /**
     * @brief CHECK 命令处理器
     * @details 手动触发设备自检，调用 CheckerRunner 执行检查并返回结果。
     */
    class CheckHandler : public ICommandHandler {
    public:
        /**
         * @brief 构造函数
         * @param ctx 运行期依赖上下文，从中获取自检配置文件路径（selftest.json）
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
