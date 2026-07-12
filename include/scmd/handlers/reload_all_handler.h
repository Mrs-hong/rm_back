/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    struct ReloadAllRequest {};

    template <>
    struct RequestCommand<ReloadAllRequest> { static constexpr ScmCommand value = ScmCommand::RELOAD_ALL; };

    inline Json::Value ToJson(const ReloadAllRequest& /*req*/) {
        return Json::Value(Json::objectValue);
    }

    /**
     * @brief RELOAD_ALL 命令处理器
     * @details 重载所有已安装服务的配置。
     */
    class ReloadAllHandler : public ICommandHandler {
    public:
        explicit ReloadAllHandler(const HandlerContext&) {}

        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           const ServiceContext& ctx,
                           KeyOperationRecorder& recorder) override;
    };

}  // namespace qifeng::scm
