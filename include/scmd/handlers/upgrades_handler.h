/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief UPGRADES 命令请求参数
     */
    struct UpgradesRequest {
        std::string serviceName;  // 服务名称
        std::string tarDir;       // 外部升级素材目录/tar包路径（空表示使用服务内部soft_dir）
    };

    template <>
    struct RequestCommand<UpgradesRequest> { static constexpr ScmCommand value = ScmCommand::UPGRADES; };

    inline Json::Value ToJson(const UpgradesRequest& req) {
        Json::Value root;
        root["serviceName"] = req.serviceName;
        root["tarDir"] = req.tarDir;
        return root;
    }

    /**
     * @brief UPGRADES 命令处理器
     * @details 使用服务内部预置升级包执行升级，并在操作前后记录关键操作日志。
     *          流程：从服务 upgrade.soft_dir 查找升级包 → 解析版本 → 升级 → 写入结果文件。
     */
    class UpgradesHandler : public ICommandHandler {
    public:
        explicit UpgradesHandler(const HandlerContext&) {}

        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           const ServiceContext& ctx,
                           KeyOperationRecorder& recorder) override;

        ResultMsg Recover(const KeyOperationRecord& record, const ServiceContext& ctx) override;
    };

}  // namespace qifeng::scm
