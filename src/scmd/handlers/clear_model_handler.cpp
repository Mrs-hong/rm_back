/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/clear_model_handler.h"
#include "scmd/handler_registry.h"

#include "common/types.h"
#include "ipc/data_def.h"
#include "qifeng_framework/common/logger.h"
#include "service_manager/key_recoder.h"
#include "service_manager/model_manager.h"
#include "service_manager/service_context.h"

namespace qifeng::scm {

    ScmCommand ClearModelHandler::GetCommand() const {
        return ScmCommand::CLEAR_MODEL;
    }

    ScmResponse ClearModelHandler::Handle(const ScmRequest& request,
                                            const ServiceContext& ctx,
                                            KeyOperationRecorder& /*recorder*/) {
        const auto* params = std::get_if<ClearModelRequest>(&request.data);
        if (params == nullptr || params->modelName.empty()) {
            SLOG_WARN << "clear_model command missing model name";
            ScmResponse response;
            response.code = -1;
            response.message = "clear_model requires --name";
            return response;
        }

        // 模型名合法性由 ServiceManager::ValidateModelName 统一校验，
        // 这里仅做基础非空检查，避免重复逻辑。

        ScmResponse response;
        auto result = ctx.modelManager->ClearModel(params->modelName);
        response.code = result.code;
        response.message = result.msg;
        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::CLEAR_MODEL, ClearModelHandler)

}  // namespace qifeng::scm
