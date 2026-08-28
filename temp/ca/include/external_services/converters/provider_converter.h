#ifndef QIFENG_CA_EXTERNAL_SERVICES_CONVERTERS_PROVIDER_CONVERTER_H
#define QIFENG_CA_EXTERNAL_SERVICES_CONVERTERS_PROVIDER_CONVERTER_H

#include <string>

#include "external_services/service_types.h"

namespace qifeng_ca::external_services {

    // 通用配置
    struct ProviderConfig {
        std::string mAppId;
        std::string mSecretKey;
        std::string mRegion;
        std::string mEndpoint;
        std::string mModel;
    };

    // 通用转换接口: 通过模板参数区分转写/总结, 同一类不同模板参数
    // 不同如下：
    //   - 转写: Converter<Req, Resp, ServiceRequest, ServiceResponse>
    //   - 总结: Converter<Req, Resp, SummaryRequest, SummaryResult>
    template <typename Req, typename Resp, typename ServiceReq = ServiceRequest, typename ServiceResp = ServiceResponse>
    class Converter {
    public:
        virtual ~Converter() = default;

        // 统一请求: 将 ServiceReq 转换为服务商协议请求 Req
        virtual Req BuildRequest(const ServiceReq &in, const ProviderConfig &cfg) = 0;

        // 统一响应: 将服务商协议响应 Resp 转换为 ServiceResp
        virtual ServiceResp ParseResponse(const Resp &out, const ProviderConfig &cfg) = 0;
    };

}  // namespace qifeng_ca::external_services

#endif  // QIFENG_CA_EXTERNAL_SERVICES_CONVERTERS_PROVIDER_CONVERTER_H
