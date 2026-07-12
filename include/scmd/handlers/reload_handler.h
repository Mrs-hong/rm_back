/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief RELOAD 命令请求参数
     */
    struct ReloadRequest {
        std::string serviceName;  // 服务名称
    };

    template <>
    struct RequestCommand<ReloadRequest> {
        static constexpr ScmCommand value = ScmCommand::RELOAD;
    };

    inline Json::Value ToJson(const ReloadRequest& req) {
        Json::Value root;
        root["serviceName"] = req.serviceName;
        return root;
    }

    /**
     * @brief RELOAD 命令处理器
     * @details 重载指定单个服务的配置。
     */
    class ReloadHandler : public ICommandHandler {
    public:
        explicit ReloadHandler(const HandlerContext&) {}

        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           const ServiceContext& ctx,
                           KeyOperationRecorder& recorder) override;
    };

}  // namespace qifeng::scm
