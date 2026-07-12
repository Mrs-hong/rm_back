/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/add_model_handler.h"

#include "common/types.h"
#include "qifeng_framework/common/logger.h"
#include "scmd/handler_registry.h"
#include "service_manger/key_recoder.h"
#include "service_manger/model_manager.h"
#include "service_manger/service_context.h"

namespace qifeng::scm {

    static std::optional<AddModelRequest> FromJson(const Json::Value& params) {
        AddModelRequest req;
        if (params.isMember("srcPath") && params["srcPath"].isString()) {
            req.srcPath = params["srcPath"].asString();
        }
        return req;
    }

    ScmCommand AddModelHandler::GetCommand() const {
        return ScmCommand::ADD_MODEL;
    }

    ScmResponse AddModelHandler::Handle(const ScmRequest& request,
                                        const ServiceContext& ctx,
                                        KeyOperationRecorder& /*recorder*/) {
        auto paramsOpt = FromJson(request.params);
        if (!paramsOpt.has_value() || paramsOpt->srcPath.empty()) {
            SLOG_WARN << "add_model command missing source path";
            ScmResponse response;
            response.code = -1;
            response.message = "add_model requires a source path (directory or tar.gz)";
            return response;
        }

        ScmResponse response;
        auto result = ctx.modelManager->AddModel(paramsOpt->srcPath);
        response.code = result.code;
        response.message = result.msg;
        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::ADD_MODEL, AddModelHandler)

}  // namespace qifeng::scm
