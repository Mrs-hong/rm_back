/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief STOP 命令请求参数
     */
    struct StopRequest {
        std::string serviceName;  // 服务名称
    };

    template <>
    struct RequestCommand<StopRequest> {
        static constexpr ScmCommand value = ScmCommand::STOP;
    };

    inline Json::Value ToJson(const StopRequest& req) {
        Json::Value root;
        root["serviceName"] = req.serviceName;
        return root;
    }

    /**
     * @brief STOP 命令处理器
     * @details 停止指定服务，并在操作前后记录关键操作日志。
     */
    class StopHandler : public ICommandHandler {
    public:
        explicit StopHandler(const HandlerContext&) {}

        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           const ServiceContext& ctx,
                           KeyOperationRecorder& recorder) override;

        ResultMsg Recover(const KeyOperationRecord& record, const ServiceContext& ctx) override;
    };

}  // namespace qifeng::scm
