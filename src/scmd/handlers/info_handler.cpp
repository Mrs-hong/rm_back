/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handler_registry.h"
#include "scmd/handlers/info_handler.h"

#include "common/scmd_types.h"
#include "common/service_error_info.h"
#include "common/types.h"
#include "ipc/data_def.h"
#include "qifeng_framework/common/logger.h"
#include "service_manager/key_recoder.h"
#include "service_manager/service_context.h"
#include "service_manager/service_manager.h"

#include <iomanip>
#include <sstream>
#include <thread>

namespace qifeng::scm {

    ScmCommand InfoHandler::GetCommand() const {
        return ScmCommand::INFO;
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ScmResponse InfoHandler::Handle(const ScmRequest &request, const ServiceContext &ctx,
                                    KeyOperationRecorder & /*recorder*/) {
        const auto* params = std::get_if<InfoRequest>(&request.data);
        if (params == nullptr || params->serviceName.empty()) {
            SLOG_WARN << "Info command missing service name";
            ScmResponse response;
            response.code = -1;
            response.message = "Info command requires service name";
            return response;
        }

        ScmResponse response;
        auto result = ctx.serviceManager->GetServiceStatus(params->serviceName);
        response.code = result.code;
        if (result.IsDefaultSuccess()) {
            auto info = ctx.serviceManager->GetServiceRuntimeInfo(params->serviceName);
            if (info.pid > 0 || params->infoDetail) {
                response.message = "success";
                Json::Value root;
                root["serviceName"] = params->serviceName;
                root["version"] = info.currentVersion;
                root["pid"] = static_cast<int>(info.pid);
                root["status"] = info.status;
                root["startTime"] = info.startTime;
                root["runTime"] = info.runTime;
                // 内存使用量：格式化字节为 MB，保留两位小数
                {
                    double memMB = static_cast<double>(info.memoryUsage) / (1024.0 * 1024.0);
                    std::ostringstream memStream;
                    memStream << std::fixed << std::setprecision(2) << memMB << " MB";
                    root["memoryUsage"] = memStream.str();
                }
                // CPU 使用率：格式化百分比并标注核心数
                {
                    unsigned int numCores = std::thread::hardware_concurrency();
                    double cpuPercent = static_cast<double>(info.cpuUsage);
                    std::ostringstream cpuStream;
                    cpuStream << std::fixed << std::setprecision(2) << cpuPercent << "%";
                    if (numCores > 0) {
                        cpuStream << " (" << numCores << "-core)";
                    }
                    root["cpuUsage"] = cpuStream.str();
                }
                root["configFilePath"] = info.configFilePath;
                root["rootPath"] = info.rootPath;
                root["dbFilePath"] = info.dbFilePath;
                root["recoveryCount"] = info.recoveryCount;

                // --error 参数：附加错误诊断信息到响应末尾
                if (params->infoDetail) {
                    ServiceErrorInfo errInfo;
                    errInfo.subState = info.subState;
                    errInfo.result = info.errorResult;
                    errInfo.exitCode = info.exitCode;
                    errInfo.exitStatus = info.exitStatus;
                    std::string errorMsg = ServiceErrorInfo::FormatError(errInfo, info.status, info.recoveryCount);
                    if (!errorMsg.empty()) {
                        root["errorInfo"] = errorMsg;
                    }
                }

                response.data = std::move(root);
            } else {
                response.message = "service is inactive";
            }
        } else {
            response.message = result.msg;
        }
        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::INFO, InfoHandler)

}  // namespace qifeng::scm
