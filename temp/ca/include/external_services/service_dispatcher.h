//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_EXTERNAL_SERVICES_SERVICE_DISPATCHER_H
#define QIFENG_CA_EXTERNAL_SERVICES_SERVICE_DISPATCHER_H

#include "external_services/external_service.h"

namespace qifeng_ca::external_services {

    // 服务调度器
    class ServiceDispatcher {
    public:
        static ServiceDispatcher &GetInstance();

        // 转写: 当前方式、外部HTTP
        bool UseExternalTranscribe() const;
        // 总结: 当前方式、外部HTTP
        bool UseExternalSummary() const;

        // 获取已创建的外部转写服务
        TranscribeServicePtr GetTranscribeService();
        // 获取已创建的外部总结服务
        SummaryServicePtr GetSummaryService();

    private:
        ServiceDispatcher() = default;
        ~ServiceDispatcher() = default;
        ServiceDispatcher(const ServiceDispatcher &) = delete;
        ServiceDispatcher &operator=(const ServiceDispatcher &) = delete;
        ServiceDispatcher(ServiceDispatcher &&) = delete;
        ServiceDispatcher &operator=(ServiceDispatcher &&) = delete;

        TranscribeServicePtr mTranscribeService;
        SummaryServicePtr mSummaryService;
    };

}  // namespace qifeng_ca::external_services

#endif  // QIFENG_CA_EXTERNAL_SERVICES_SERVICE_DISPATCHER_H
