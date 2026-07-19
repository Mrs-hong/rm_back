/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/version_handler.h"
#include "scmd/handler_registry.h"

#include "common/version.hpp"
#include "ipc/data_def.h"
#include "service_manager/key_recoder.h"
#include "service_manager/service_context.h"

namespace qifeng::scm {

    ScmCommand VersionHandler::GetCommand() const {
        return ScmCommand::VERSION;
    }

    ScmResponse VersionHandler::Handle(const ScmRequest& /*request*/,
                                       const ServiceContext& /*ctx*/,
                                       KeyOperationRecorder& /*recorder*/) {
        ScmResponse response;
        const auto& versionInfo = GetVersionInfo();
        response.code = 0;
        response.message = "qifeng_scm version " + versionInfo.version;
        response.data["version"] = versionInfo.version;
        response.data["buildTime"] = versionInfo.buildTime;
        response.data["gitCommit"] = versionInfo.gitCommit;
        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::VERSION, VersionHandler)

}  // namespace qifeng::scm
