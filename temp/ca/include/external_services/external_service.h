//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_EXTERNAL_SERVICES_EXTERNAL_SERVICE_H
#define QIFENG_CA_EXTERNAL_SERVICES_EXTERNAL_SERVICE_H

#include <functional>
#include <memory>
#include <string_view>

#include "external_services/service_types.h"

namespace qifeng_ca::external_services {

    // 通用抽象基类
    //   - 转写: ExternalService<ServiceRequest, ServiceResponse>
    //   - 总结: ExternalService<SummaryRequest, SummaryResult>
    template <typename ServiceReq, typename ServiceResp>
    class ExternalService {
    public:
        using RequestType = ServiceReq;
        using ResponseType = ServiceResp;
        using Callback = std::function<void(const ServiceResp &)>;

        virtual ~ExternalService() = default;

        // http调用的服务名（通过服务名查询配置、获取、创建对象）
        virtual std::string_view GetProviderName() const = 0;

        // 服务类型
        virtual ServiceType GetServiceType() const = 0;

        // 同步执行
        virtual ServiceResp Execute(const ServiceReq &req) = 0;

        // 异步执行
        virtual void ExecuteAsync(const ServiceReq &req, Callback cb) = 0;
    };

    // 转写/总结服务类型别名
    using TranscribeService = ExternalService<ServiceRequest, ServiceResponse>;
    using SummaryService = ExternalService<SummaryRequest, SummaryResult>;

    // 回调签名
    using TranscribeCallback = TranscribeService::Callback;
    using SummaryCallback = SummaryService::Callback;

    // 服务指针
    using TranscribeServicePtr = std::shared_ptr<TranscribeService>;
    using SummaryServicePtr = std::shared_ptr<SummaryService>;

}  // namespace qifeng_ca::external_services

#endif  // QIFENG_CA_EXTERNAL_SERVICES_EXTERNAL_SERVICE_H
