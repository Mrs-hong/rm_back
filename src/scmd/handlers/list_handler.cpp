/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/list_handler.h"

#include "common/config.h"
#include "scmd/handler_registry.h"
#include "service_manger/key_recoder.h"
#include "service_manger/service_context.h"
#include "service_manger/service_manager.h"

namespace qifeng::scm {

    ScmCommand ListHandler::GetCommand() const {
        return ScmCommand::LIST;
    }

    ScmResponse ListHandler::Handle(const ScmRequest& /*request*/,
                                    const ServiceContext& ctx,
                                    KeyOperationRecorder& /*recorder*/) {
        ScmResponse response;
        auto allServices = ctx.configLoader->GetAllServices();
        Json::Value servicesArray(Json::arrayValue);
        for (const auto& svc : allServices) {
            Json::Value svcJson;
            svcJson["serviceName"] = svc.serviceName;
            svcJson["version"] = svc.version;
            svcJson["isAutoStart"] = svc.isAutoStart;
            svcJson["active"] = ctx.serviceManager->IsServiceActive(svc.serviceName);
            servicesArray.append(svcJson);
        }
        response.code = 0;
        response.message = allServices.empty() ? "No services installed" : "success";
        response.data = servicesArray;
        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::LIST, ListHandler)

}  // namespace qifeng::scm
