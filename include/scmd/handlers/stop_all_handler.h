/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    struct StopAllRequest {};

    template <>
    struct RequestCommand<StopAllRequest> { static constexpr ScmCommand value = ScmCommand::STOP_ALL; };

    inline Json::Value ToJson(const StopAllRequest& /*req*/) {
        return Json::Value(Json::objectValue);
    }

    /**
     * @brief STOP_ALL 命令处理器
     * @details 停止所有已安装服务。
     */
    class StopAllHandler : public ICommandHandler {
    public:
        explicit StopAllHandler(const HandlerContext&) {}

        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           const ServiceContext& ctx,
                           KeyOperationRecorder& recorder) override;
    };

}  // namespace qifeng::scm
