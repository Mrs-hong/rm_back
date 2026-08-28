//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_EXTERNAL_SERVICES_HTTP_DROGON_HTTP_CLIENT_H
#define QIFENG_CA_EXTERNAL_SERVICES_HTTP_DROGON_HTTP_CLIENT_H

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "drogon/HttpClient.h"
#include "drogon/HttpRequest.h"
#include "drogon/HttpResponse.h"

namespace qifeng_ca::external_services {

    // 基于 drogon 的 HTTP 客户端封装
    class DrogonHttpClient {
    public:
        struct Config {
            std::string mHost;  // 支持URL
            uint16_t mPort {80};
            bool mUseSsl {false};
            int mTimeoutSec {60};
        };

        DrogonHttpClient() = default;
        explicit DrogonHttpClient(const Config &cfg);

        // 同步发送
        std::pair<drogon::ReqResult, drogon::HttpResponsePtr> Send(const drogon::HttpRequestPtr &req);

        // 异步发送
        using ResponseCallback = std::function<void(drogon::ReqResult, const drogon::HttpResponsePtr &)>;
        void SendAsync(const drogon::HttpRequestPtr &req, ResponseCallback cb);

        // 默认使用 POST + JSON
        drogon::HttpRequestPtr NewRequest();

        // 设置通用 header
        static void SetHeader(const drogon::HttpRequestPtr &req, const std::string &key, const std::string &val);
        // 写入 JSON body
        static void SetJsonBody(const drogon::HttpRequestPtr &req, const std::string &json);

    private:
        drogon::HttpClientPtr mClient;
        int mTimeoutSec {60};
    };

}  // namespace qifeng_ca::external_services

#endif  // QIFENG_CA_EXTERNAL_SERVICES_HTTP_DROGON_HTTP_CLIENT_H
