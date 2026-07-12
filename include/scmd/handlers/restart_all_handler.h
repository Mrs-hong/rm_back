/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    struct RestartAllRequest {};

    template <>
    struct RequestCommand<RestartAllRequest> { static constexpr ScmCommand value = ScmCommand::RESTART_ALL; };

    inline Json::Value ToJson(const RestartAllRequest& /*req*/) {
        return Json::Value(Json::objectValue);
    }

    /**
     * @brief RESTART_ALL 命令处理器
     * @details 重启所有已安装服务。
     */
    class RestartAllHandler : public ICommandHandler {
    public:
        explicit RestartAllHandler(const HandlerContext&) {}

        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           const ServiceContext& ctx,
                           KeyOperationRecorder& recorder) override;
    };

}  // namespace qifeng::scm
