/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief INFO 命令请求参数
     */
    struct InfoRequest {
        std::string serviceName;   // 服务名称
        bool infoDetail {false};   // 是否显示错误详情
    };

    template <>
    struct RequestCommand<InfoRequest> { static constexpr ScmCommand value = ScmCommand::INFO; };

    inline Json::Value ToJson(const InfoRequest& req) {
        Json::Value root;
        root["serviceName"] = req.serviceName;
        root["infoDetail"] = req.infoDetail;
        return root;
    }

    /**
     * @brief INFO 命令处理器
     * @details 返回指定服务的运行时详情，支持附加错误诊断信息。
     */
    class InfoHandler : public ICommandHandler {
    public:
        explicit InfoHandler(const HandlerContext&) {}

        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           const ServiceContext& ctx,
                           KeyOperationRecorder& recorder) override;
    };

}  // namespace qifeng::scm
