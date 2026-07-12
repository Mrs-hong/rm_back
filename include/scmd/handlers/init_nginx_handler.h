/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief INIT_NGINX 命令请求参数
     */
    struct InitNginxRequest {
        std::string dirPath;      // nginx 配置源路径（目录或 tar.gz）
    };

    template <>
    struct RequestCommand<InitNginxRequest> { static constexpr ScmCommand value = ScmCommand::INIT_NGINX; };

    inline Json::Value ToJson(const InitNginxRequest& req) {
        Json::Value root;
        root["dirPath"] = req.dirPath;
        return root;
    }

    /**
     * @brief INIT_NGINX 命令处理器
     * @details 独立配置服务的 nginx，安装前端配置到系统 nginx 目录。
     */
    class InitNginxHandler : public ICommandHandler {
    public:
        explicit InitNginxHandler(const HandlerContext&) {}

        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           const ServiceContext& ctx,
                           KeyOperationRecorder& recorder) override;
    };

}  // namespace qifeng::scm
