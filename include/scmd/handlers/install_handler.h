/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief INSTALL 命令请求参数
     */
    struct InstallRequest {
        std::string serviceName;  // 服务名称
        std::string tarDir;       // tar包目录
    };

    template <>
    struct RequestCommand<InstallRequest> { static constexpr ScmCommand value = ScmCommand::INSTALL; };

    inline Json::Value ToJson(const InstallRequest& req) {
        Json::Value root;
        root["serviceName"] = req.serviceName;
        root["tarDir"] = req.tarDir;
        return root;
    }

    /**
     * @brief INSTALL 命令处理器
     * @details 安装指定服务，并在操作前后记录关键操作日志。
     */
    class InstallHandler : public ICommandHandler {
    public:
        explicit InstallHandler(const HandlerContext&) {}

        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           const ServiceContext& ctx,
                           KeyOperationRecorder& recorder) override;

        ResultMsg Recover(const KeyOperationRecord& record, const ServiceContext& ctx) override;
    };

}  // namespace qifeng::scm
