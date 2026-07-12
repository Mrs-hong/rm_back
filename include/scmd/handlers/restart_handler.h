/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief RESTART 命令请求参数
     */
    struct RestartRequest {
        std::string serviceName;  // 服务名称
    };

    template <>
    struct RequestCommand<RestartRequest> {
        static constexpr ScmCommand value = ScmCommand::RESTART;
    };

    inline Json::Value ToJson(const RestartRequest& req) {
        Json::Value root;
        root["serviceName"] = req.serviceName;
        return root;
    }

    /**
     * @brief RESTART 命令处理器
     * @details 重启指定单个服务。
     */
    class RestartHandler : public ICommandHandler {
    public:
        explicit RestartHandler(const HandlerContext&) {}

        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           const ServiceContext& ctx,
                           KeyOperationRecorder& recorder) override;
    };

}  // namespace qifeng::scm
