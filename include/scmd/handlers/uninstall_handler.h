/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief UNINSTALL 命令请求参数
     */
    struct UninstallRequest {
        std::string serviceName;  // 服务名称
    };

    template <>
    struct RequestCommand<UninstallRequest> {
        static constexpr ScmCommand value = ScmCommand::UNINSTALL;
    };

    inline Json::Value ToJson(const UninstallRequest& req) {
        Json::Value root;
        root["serviceName"] = req.serviceName;
        return root;
    }

    /**
     * @brief UNINSTALL 命令处理器
     * @details 卸载指定服务，并在操作前后记录关键操作日志。
     */
    class UninstallHandler : public ICommandHandler {
    public:
        explicit UninstallHandler(const HandlerContext&) {}

        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           const ServiceContext& ctx,
                           KeyOperationRecorder& recorder) override;

        ResultMsg Recover(const KeyOperationRecord& record, const ServiceContext& ctx) override;
    };

}  // namespace qifeng::scm
