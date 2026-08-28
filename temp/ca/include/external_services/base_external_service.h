//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_EXTERNAL_SERVICES_BASE_EXTERNAL_SERVICE_H
#define QIFENG_CA_EXTERNAL_SERVICES_BASE_EXTERNAL_SERVICE_H

#include <string>

#include "drogon/HttpRequest.h"
#include "drogon/HttpResponse.h"
#include "external_services/external_service.h"
#include "external_services/http/drogon_http_client.h"

namespace qifeng_ca::external_services {

    // 模板基类。通过不同的实例实现转写、总结
    template <typename Derived, typename ServiceReq, typename ServiceResp>
    class BaseExternalService : public ExternalService<ServiceReq, ServiceResp> {
        using Base = ExternalService<ServiceReq, ServiceResp>;

    public:
        ServiceResp Execute(const ServiceReq &req) override {
            auto httpReq = Impl().BuildRequest(req);
            auto [result, resp] = mHttpClient.Send(httpReq);
            if (result != drogon::ReqResult::Ok || !resp) {
                return MakeError(result, "sync request failed");
            }
            return Impl().ParseResponse(resp, req);
        }

        void ExecuteAsync(const ServiceReq &req, typename Base::Callback cb) override {
            auto httpReq = Impl().BuildRequest(req);
            // 生命周期由 ServiceDispatcher 持有，保证不会出现空指针
            mHttpClient.SendAsync(httpReq, [this, req, cb](drogon::ReqResult r, const drogon::HttpResponsePtr &resp) {
                ServiceResp out;
                if (r != drogon::ReqResult::Ok || !resp) {
                    out = MakeError(r, "async request failed");
                } else {
                    out = Impl().ParseResponse(resp, req);
                }
                if (cb) {
                    cb(out);
                }
            });
        }

    protected:
        DrogonHttpClient mHttpClient;

        Derived &Impl() { return static_cast<Derived &>(*this); }
        const Derived &Impl() const { return static_cast<const Derived &>(*this); }

        // 将 drogon 请求结果转换为统一错误响应
        // 依赖 ServiceResp 含 mSuccess/mError.mCode/mError.mMessage(ServiceResponse 与 SummaryResult 均满足)
        ServiceResp MakeError(drogon::ReqResult result, const std::string &msg) {
            ServiceResp resp;
            resp.mSuccess = false;
            resp.mError.mCode = static_cast<int>(result);
            resp.mError.mMessage = msg;
            return resp;
        }
    };

}  // namespace qifeng_ca::external_services

#endif  // QIFENG_CA_EXTERNAL_SERVICES_BASE_EXTERNAL_SERVICE_H
