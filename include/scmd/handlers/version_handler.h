/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    struct VersionRequest {};

    template <>
    struct RequestCommand<VersionRequest> { static constexpr ScmCommand value = ScmCommand::VERSION; };

    inline Json::Value ToJson(const VersionRequest& /*req*/) {
        return Json::Value(Json::objectValue);
    }

    /**
     * @brief VERSION 命令处理器
     * @details 返回 qifeng_scm 的版本、构建时间和 Git commit
     */
    class VersionHandler : public ICommandHandler {
    public:
        explicit VersionHandler(const HandlerContext&) {}

        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           const ServiceContext& ctx,
                           KeyOperationRecorder& recorder) override;
    };

}  // namespace qifeng::scm
