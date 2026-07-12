/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief SLOG 命令请求参数
     */
    struct SlogRequest {
        std::string serviceName;  // 服务名称
        int logCount {0};         // 日志行数
    };

    template <>
    struct RequestCommand<SlogRequest> { static constexpr ScmCommand value = ScmCommand::SLOG; };

    inline Json::Value ToJson(const SlogRequest& req) {
        Json::Value root;
        root["serviceName"] = req.serviceName;
        root["logCount"] = req.logCount;
        return root;
    }

    /**
     * @brief SLOG 命令处理器
     * @details 返回指定服务的 systemd journal 日志。
     */
    class SlogHandler : public ICommandHandler {
    public:
        explicit SlogHandler(const HandlerContext&) {}

        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           const ServiceContext& ctx,
                           KeyOperationRecorder& recorder) override;
    };

}  // namespace qifeng::scm
