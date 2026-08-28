//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_EXTERNAL_SERVICES_SUMMARY_SAMPLE_SUMMARY_SERVICE_H
#define QIFENG_CA_EXTERNAL_SERVICES_SUMMARY_SAMPLE_SUMMARY_SERVICE_H

#include <string>
#include <string_view>

#include "json/value.h"

#include "external_services/base_external_service.h"
#include "external_services/converters/json_helpers.h"
#include "external_services/converters/provider_converter.h"
#include "external_services/http/drogon_http_client.h"
#include "external_services/http/endpoint_parser.h"
#include "external_services/service_types.h"

namespace qifeng_ca::external_services {

    // 总结服务实例
    class SampleSummaryService : public BaseExternalService<SampleSummaryService, SummaryRequest, SummaryResult> {
    public:
        explicit SampleSummaryService(const ProviderConfig &cfg) : SampleSummaryService(cfg, "sample", "/v1/summary") {}

        std::string_view GetProviderName() const override { return mProviderName; }
        ServiceType GetServiceType() const override { return ServiceType::Summary; }

        // 构建HTTP请求(POST + JSON)
        drogon::HttpRequestPtr BuildRequest(const SummaryRequest &req) {
            auto httpReq = mHttpClient.NewRequest();
            httpReq->setPath(mPath);
            ApplyAuth(httpReq);
            DrogonHttpClient::SetJsonBody(httpReq, BuildRequestBody(req));
            return httpReq;
        }

        // 解析响应
        SummaryResult ParseResponse(const drogon::HttpResponsePtr &resp, const SummaryRequest &req) {
            SummaryResult out;
            out.mRawResponse = std::string(resp->body());
            Json::Value root;
            if (!ParseJson(out.mRawResponse, root) || !root.isObject()) {
                out.mSuccess = false;
                out.mError.mCode = static_cast<int>(drogon::ReqResult::Ok);
                out.mError.mMessage = "summary response is not valid json";
                return out;
            }
            if (root.isMember("code") && root["code"].asInt() != 0) {
                out.mSuccess = false;
                out.mError.mCode = root["code"].asInt();
                out.mError.mMessage = root.get("message", "summary failed").asString();
                return out;
            }
            out.mSuccess = true;
            out.mOverview = root.get("overview", "").asString();
            const Json::Value &kws = root["keywords"];
            if (kws.isArray()) {
                for (const auto &kw : kws) {
                    out.mKeywords.push_back(kw.asString());
                }
            }
            return out;
        }

    protected:
        SampleSummaryService(const ProviderConfig &cfg, std::string_view name, std::string_view path)
            : mConfig(cfg), mProviderName(name), mPath(path) {
            InitHttpClient(cfg.mEndpoint, 60);
        }

        ProviderConfig mConfig;

    private:
        std::string mProviderName;
        std::string mPath;

        void InitHttpClient(const std::string &endpoint, int timeoutSec) {
            DrogonHttpClient::Config httpCfg;
            httpCfg.mHost = ParseHost(endpoint);
            httpCfg.mPort = ParsePort(endpoint);
            httpCfg.mUseSsl = UseSsl(endpoint);
            httpCfg.mTimeoutSec = timeoutSec;
            mHttpClient = DrogonHttpClient(httpCfg);
        }

        void ApplyAuth(const drogon::HttpRequestPtr &req) const {
            if (!mConfig.mAppId.empty()) {
                DrogonHttpClient::SetHeader(req, "X-App-Id", mConfig.mAppId);
            }
            if (!mConfig.mSecretKey.empty()) {
                DrogonHttpClient::SetHeader(req, "X-Secret-Key", mConfig.mSecretKey);
            }
            if (!mConfig.mModel.empty()) {
                DrogonHttpClient::SetHeader(req, "X-Model", mConfig.mModel);
            }
        }

        std::string BuildRequestBody(const SummaryRequest &req) const {
            Json::Value body(Json::objectValue);
            body["provider"] = std::string(mProviderName);
            body["text"] = req.mText;
            body["date"] = req.mDate;
            body["location"] = req.mLocation;
            body["host"] = req.mHost;
            body["attendees"] = req.mAttendees;
            if (!req.mModel.empty()) {
                body["model"] = req.mModel;
            }
            return ToJsonString(body);
        }
    };

}  // namespace qifeng_ca::external_services

#endif  // QIFENG_CA_EXTERNAL_SERVICES_SUMMARY_SAMPLE_SUMMARY_SERVICE_H
