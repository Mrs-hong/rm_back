/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "common/scmd_types.h"
#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief RESET_NGINX 命令请求参数
     */
    struct ResetNginxRequest {
        NginxResetMode mode {NginxResetMode::BACK};
    };

    template <>
    struct RequestCommand<ResetNginxRequest> { static constexpr ScmCommand value = ScmCommand::RESET_NGINX; };

    inline Json::Value ToJson(const ResetNginxRequest& req) {
        Json::Value root;
        root["mode"] = static_cast<int>(req.mode);
        return root;
    }

    /**
     * @brief RESET_NGINX 命令处理器
     * @details 重置服务的 nginx 配置，删除系统 conf 和服务 nginx 目录。
     */
    class ResetNginxHandler : public ICommandHandler {
    public:
        explicit ResetNginxHandler(const HandlerContext&) {}

        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           const ServiceContext& ctx,
                           KeyOperationRecorder& recorder) override;
    };

}  // namespace qifeng::scm
