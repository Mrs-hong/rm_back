/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <cstdio>
#include <utility>
#include <workflow/HttpMessage.h>
#include <workflow/HttpUtil.h>
#include <workflow/WFGlobal.h>
#include <workflow/WFTaskFactory.h>

#include "common/logger.h"
#include "http/http.h"

namespace qifeng {

    HttpResponse Http::SyncRequest(const HttpRequest& req) {
        HttpResponse result;
        WFFacilities::WaitGroup wg(1);
        AsyncRequest(req, [&](const HttpResponse& rsp) {
            result = rsp;
            wg.done();
        });
        wg.wait();
        return result;
    }

    void Http::OnHttpTaskFinished(WFHttpTask* task, HttpCallback cb) {
        HttpResponse rsp;
        try {
            auto* resp = task->get_resp();
            // status_code
            try {
                rsp.mStatusCode = std::stoi(resp->get_status_code());
            } catch (const std::exception& e) {
                SLOG_ERROR << "Invalid status code: " << resp->get_status_code() << ", error: " << e.what();
                rsp.mStatusCode = 0;
            }
            // body: 接收报文应读取 parsed body，而不是 output body
            const void* bodyPtr = nullptr;
            size_t bodySize = 0;
            if (resp->get_parsed_body(&bodyPtr, &bodySize) && bodyPtr != nullptr && bodySize > 0) {
                rsp.mBody.assign(static_cast<const char*>(bodyPtr), bodySize);
            } else {
                rsp.mBody.clear();
            }
            // headers
            protocol::HttpHeaderCursor cursor(resp);
            protocol::HttpMessageHeader header {};
            while (cursor.next(&header)) {
                const std::string name(static_cast<const char*>(header.name), header.name_len);
                const std::string value(static_cast<const char*>(header.value), header.value_len);
                rsp.mHeaders[name] = value;
            }
        } catch (const std::exception& e) {
            SLOG_ERROR << "Exception in OnHttpTaskFinished: " << e.what();
            rsp.mStatusCode = 0;
            rsp.mBody = std::string("Exception in OnHttpTaskFinished: ") + e.what();
        } catch (...) {
            SLOG_ERROR << "Unknown exception in OnHttpTaskFinished";
            rsp.mStatusCode = 0;
            rsp.mBody = "Unknown exception in OnHttpTaskFinished";
        }
        cb(rsp);
    }

    void Http::AsyncRequest(const HttpRequest& req, HttpCallback cb) {
        try {
            auto* task = WFTaskFactory::create_http_task(
                req.mUrl, Http::DefaultRedirectMax, Http::DefaultRetryMax,
                [cb](WFHttpTask* http_task) { Http::OnHttpTaskFinished(http_task, cb); });
            auto* wfReq = task->get_req();
            // 枚举转字符串
            const std::string methodStr = HttpMethodToString(req.mMethod);
            wfReq->set_method(methodStr);      // 设置HTTP方法
            wfReq->set_request_uri(req.mUrl);  // 设置URL（包含路径和查询参数）
            if (!req.mBody.empty()) {          // 设置请求体
                wfReq->append_output_body(req.mBody);
            }
            // 设置超时时间
            if (req.mTimeoutMs > 0) {
                task->set_receive_timeout(req.mTimeoutMs);
            } else {
                task->set_receive_timeout(Http::DefaultTimeoutMs);
            }
            // 设置代理
            if (!req.mProxy.empty()) {
                // workflow不支持设置代理
            }
            // 自动添加SSE相关头部（如需SSE可根据实际条件添加判断）
            wfReq->add_header_pair("Accept", "text/event-stream");
            wfReq->add_header_pair("Connection", "keep-alive");
            for (const auto& kv : req.mHeaders) {
                wfReq->add_header_pair(kv.first, kv.second);
            }
            task->start();
        } catch (const std::exception& e) {
            SLOG_ERROR << "Exception in AsyncRequest: " << e.what();
            HttpResponse rsp;
            rsp.mStatusCode = 0;
            rsp.mBody = std::string("Exception in AsyncRequest: ") + e.what();
            cb(rsp);
        } catch (...) {
            SLOG_ERROR << "Unknown exception in AsyncRequest";
            HttpResponse rsp;
            rsp.mStatusCode = 0;
            rsp.mBody = "Unknown exception in AsyncRequest";
            cb(rsp);
        }
    }

    // 字符串转枚举，未知类型返回HttpMethod::UNKNOWN并打印警告
    HttpMethod Http::StringToHttpMethod(const std::string& method_str) {
        if (method_str == "GET" || method_str == "get") {
            return HttpMethod::GET;
        }
        if (method_str == "POST" || method_str == "post") {
            return HttpMethod::POST;
        }
        if (method_str == "PUT" || method_str == "put") {
            return HttpMethod::PUT;
        }
        if (method_str == "DELETE" || method_str == "delete") {
            return HttpMethod::DELETE;
        }
        if (method_str == "HEAD" || method_str == "head") {
            return HttpMethod::HEAD;
        }
        if (method_str == "PATCH" || method_str == "patch") {
            return HttpMethod::PATCH;
        }
        if (method_str == "OPTIONS" || method_str == "options") {
            return HttpMethod::OPTIONS;
        }
        SLOG_WARN << "Unknown HTTP method string: '" << method_str << "', fallback to HttpMethod::UNKNOWN";
        return HttpMethod::UNKNOWN;
    }

    // 枚举转字符串，未知类型返回GET并打印警告
    std::string Http::HttpMethodToString(HttpMethod method) {
        switch (method) {
            case HttpMethod::GET:
                return "GET";
            case HttpMethod::POST:
                return "POST";
            case HttpMethod::PUT:
                return "PUT";
            case HttpMethod::DELETE:
                return "DELETE";
            case HttpMethod::HEAD:
                return "HEAD";
            case HttpMethod::PATCH:
                return "PATCH";
            case HttpMethod::OPTIONS:
                return "OPTIONS";
            case HttpMethod::UNKNOWN:
                SLOG_WARN << "Unknown HttpMethod, fallback to GET";
                return "GET";
            default:
                SLOG_WARN << "Unknown HttpMethod, fallback to GET";
                return "GET";
        }
    }

}  // namespace qifeng
