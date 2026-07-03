/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/reload_all_handler.h"

#include "common/types.h"
#include "ipc/data_def.h"
#include "scmd/service_ctl.h"
#include "service_manger/key_recoder.h"

namespace qifeng::scm {

    ScmCommand ReloadAllHandler::GetCommand() const {
        return ScmCommand::RELOAD_ALL;
    }

    ScmResponse ReloadAllHandler::Handle(const ScmRequest& /*request*/,
                                         ServiceControl& serviceControl,
                                         KeyOperationRecorder& /*recorder*/) {
        ScmResponse response;
        // 使用空服务名表示重载全部服务配置
        auto result = serviceControl.ReloadService("");
        response.code = result.code;
        response.message = result.msg;
        return response;
    }

}  // namespace qifeng::scm
