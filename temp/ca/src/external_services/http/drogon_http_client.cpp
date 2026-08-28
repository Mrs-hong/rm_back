//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "qifeng_framework/common/logger.h"

#include "external_services/http/drogon_http_client.h"

namespace qifeng_ca::external_services {

    DrogonHttpClient::DrogonHttpClient(const Config &cfg) : mTimeoutSec(cfg.mTimeoutSec) {
        // newHttpClient 接受 host:port 形式, useSsl 由 https:// 前缀决定
        std::string scheme = cfg.mUseSsl ? "https://" : "http://";
        std::string url = scheme + cfg.mHost + ":" + std::to_string(cfg.mPort);
        mClient = drogon::HttpClient::newHttpClient(url, nullptr, cfg.mUseSsl);
    }

    std::pair<drogon::ReqResult, drogon::HttpResponsePtr> DrogonHttpClient::Send(const drogon::HttpRequestPtr &req) {
        if (!mClient) {
            SLOG_ERROR << "DrogonHttpClient: client not initialized";
            return {drogon::ReqResult::BadServerAddress, nullptr};
        }
        return mClient->sendRequest(req, static_cast<double>(mTimeoutSec));
    }

    void DrogonHttpClient::SendAsync(const drogon::HttpRequestPtr &req, ResponseCallback cb) {
        if (!mClient) {
            SLOG_ERROR << "DrogonHttpClient: client not initialized";
            drogon::HttpResponsePtr empty;
            cb(drogon::ReqResult::BadServerAddress, empty);
            return;
        }
        mClient->sendRequest(
            req, [cb](drogon::ReqResult r, const drogon::HttpResponsePtr &resp) { cb(r, resp); },
            static_cast<double>(mTimeoutSec));
    }

    drogon::HttpRequestPtr DrogonHttpClient::NewRequest() {
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setMethod(drogon::Post);
        req->addHeader("Content-Type", "application/json");
        return req;
    }

    void DrogonHttpClient::SetHeader(const drogon::HttpRequestPtr &req, const std::string &key,
                                     const std::string &val) {
        if (req) {
            req->addHeader(key, val);
        }
    }

    void DrogonHttpClient::SetJsonBody(const drogon::HttpRequestPtr &req, const std::string &json) {
        if (req) {
            req->setBody(json);
        }
    }

}  // namespace qifeng_ca::external_services
