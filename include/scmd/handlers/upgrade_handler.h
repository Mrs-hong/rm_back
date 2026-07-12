/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief UPGRADE 命令请求参数
     */
    struct UpgradeRequest {
        std::string serviceName;  // 服务名称
        std::string tarDir;       // 新版本tar包目录
    };

    template <>
    struct RequestCommand<UpgradeRequest> { static constexpr ScmCommand value = ScmCommand::UPGRADE; };

    inline Json::Value ToJson(const UpgradeRequest& req) {
        Json::Value root;
        root["serviceName"] = req.serviceName;
        root["tarDir"] = req.tarDir;
        return root;
    }

    /**
     * @brief UPGRADE 命令处理器
     * @details 升级指定服务到新版本，并在操作前后记录关键操作日志。
     */
    class UpgradeHandler : public ICommandHandler {
    public:
        explicit UpgradeHandler(const HandlerContext&) {}

        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           const ServiceContext& ctx,
                           KeyOperationRecorder& recorder) override;

        ResultMsg Recover(const KeyOperationRecord& record, const ServiceContext& ctx) override;
    };

}  // namespace qifeng::scm
