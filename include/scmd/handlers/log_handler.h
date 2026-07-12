/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief LOG 命令请求参数
     */
    struct LogRequest {
        int logLevel {0};   // 日志级别过滤
        int logCount {0};   // 日志条数
    };

    template <>
    struct RequestCommand<LogRequest> { static constexpr ScmCommand value = ScmCommand::LOG; };

    inline Json::Value ToJson(const LogRequest& req) {
        Json::Value root;
        root["logLevel"] = req.logLevel;
        root["logCount"] = req.logCount;
        return root;
    }

    /**
     * @brief LOG 命令处理器
     * @details 返回按级别和条数过滤的操作日志。
     */
    class LogHandler : public ICommandHandler {
    public:
        explicit LogHandler(const HandlerContext&) {}

        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           const ServiceContext& ctx,
                           KeyOperationRecorder& recorder) override;
    };

}  // namespace qifeng::scm
