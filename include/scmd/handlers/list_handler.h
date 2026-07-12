/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief LIST 命令请求参数（无参数）
     */
    struct ListRequest {};

    template <>
    struct RequestCommand<ListRequest> { static constexpr ScmCommand value = ScmCommand::LIST; };

    inline Json::Value ToJson(const ListRequest& /*req*/) {
        return Json::Value(Json::objectValue);
    }

    /**
     * @brief LIST 命令处理器
     * @details 返回所有已安装服务的列表及运行状态。
     */
    class ListHandler : public ICommandHandler {
    public:
        explicit ListHandler(const HandlerContext&) {}

        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           const ServiceContext& ctx,
                           KeyOperationRecorder& recorder) override;
    };

}  // namespace qifeng::scm
