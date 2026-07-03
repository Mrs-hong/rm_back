/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/handlers/list_handler.h"

#include "common/config.h"
#include "ipc/data_def.h"
#include "scmd/service_ctl.h"
#include "service_manger/key_recoder.h"

namespace qifeng::scm {

    ScmCommand ListHandler::GetCommand() const {
        return ScmCommand::LIST;
    }

    ScmResponse ListHandler::Handle(const ScmRequest& /*request*/,
                                    ServiceControl& serviceControl,
                                    KeyOperationRecorder& /*recorder*/) {
        ScmResponse response;
        auto allServices = serviceControl.GetConfigLoader().GetAllServices();
        Json::Value servicesArray(Json::arrayValue);
        for (const auto& svc : allServices) {
            Json::Value svcJson;
            svcJson["serviceName"] = svc.serviceName;
            svcJson["version"] = svc.version;
            svcJson["isAutoStart"] = svc.isAutoStart;
            svcJson["active"] = serviceControl.IsServiceActive(svc.serviceName);
            servicesArray.append(svcJson);
        }
        response.code = 0;
        response.message = allServices.empty() ? "No services installed" : "success";
        response.data = servicesArray;
        return response;
    }

}  // namespace qifeng::scm
