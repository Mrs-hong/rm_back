/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/log_handler.h"

#include "common/config.h"
#include "common/types.h"
#include "common/utils.h"
#include "scmd/handler_registry.h"
#include "service_manger/key_recoder.h"
#include "service_manger/service_context.h"

#include <fstream>

namespace qifeng::scm {

    static std::optional<LogRequest> FromJson(const Json::Value& params) {
        LogRequest req;
        if (params.isMember("logLevel") && params["logLevel"].isInt()) {
            req.logLevel = params["logLevel"].asInt();
        }
        if (params.isMember("logCount") && params["logCount"].isInt()) {
            req.logCount = params["logCount"].asInt();
        }
        return req;
    }

    ScmCommand LogHandler::GetCommand() const {
        return ScmCommand::LOG;
    }

    ScmResponse LogHandler::Handle(const ScmRequest& request,
                                   const ServiceContext& ctx,
                                   KeyOperationRecorder& /*recorder*/) {
        auto paramsOpt = FromJson(request.params);
        if (!paramsOpt.has_value()) {
            ScmResponse response;
            response.code = -1;
            response.message = "Invalid log request";
            return response;
        }

        const auto& params = *paramsOpt;

        // Read scmd.log from logsDir
        const auto& configInfo = ctx.configLoader->GetConfigInfo();
        std::string logPath = utils::JoinPath(configInfo.logsDir, "scmd.log");

        std::ifstream logFile(logPath);
        if (!logFile.is_open()) {
            ScmResponse response;
            response.code = -1;
            response.message = "Log file not found: " + logPath;
            return response;
        }

        // 读取所有行，返回最后logCount行内容
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(logFile, line)) {
            lines.push_back(line);
        }
        logFile.close();

        int startIdx = params.logCount > 0 && static_cast<int>(lines.size()) > params.logCount
                           ? static_cast<int>(lines.size()) - params.logCount : 0;

        // 将日志内容拼接到msg中
        std::string logContent;
        for (int i = startIdx; i < static_cast<int>(lines.size()); ++i) {
            if (i > startIdx) {
                logContent += "\n";
            }
            logContent += lines[static_cast<size_t>(i)];
        }

        ScmResponse response;
        response.code = 0;
        response.message = logContent.empty() ? "No log entries" : logContent;
        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::LOG, LogHandler)

}  // namespace qifeng::scm
