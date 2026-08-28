/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <cstring>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <workflow/HttpMessage.h>
#include <workflow/HttpUtil.h>
#include <workflow/WFGlobal.h>
#include <workflow/WFTaskFactory.h>

#include "common/logger.h"
#include "http/http.h"
#include "http/http_server.h"

namespace qifeng {

    // HttpServer 辅助方法实现

    ParamMap HttpServer::ParseKvParams(const std::string& source) {
        ParamMap params;
        std::istringstream stream(source);
        std::string kvPair;
        while (std::getline(stream, kvPair, '&')) {
            if (kvPair.empty()) {
                continue;
            }
            const size_t equalPos = kvPair.find('=');
            if (equalPos == std::string::npos) {
                params[kvPair] = "";
                continue;
            }
            params[kvPair.substr(0, equalPos)] = kvPair.substr(equalPos + 1);
        }
        return params;
    }

    ParamMap HttpServer::ParseQueryParamsFromUri(const char* uri) {
        if (uri == nullptr) {
            return {};
        }
        const std::string uriStr(uri);
        const size_t queryPos = uriStr.find('?');
        if (queryPos == std::string::npos || queryPos + 1 >= uriStr.size()) {
            return {};
        }
        return ParseKvParams(uriStr.substr(queryPos + 1));
    }

    ParamMap HttpServer::ParseBodyParams(protocol::HttpRequest* req) {
        const void* bodyPtr = nullptr;
        size_t bodySize = 0;
        if (!req->get_parsed_body(&bodyPtr, &bodySize) || bodyPtr == nullptr || bodySize == 0) {
            return {};
        }
        return ParseKvParams(std::string(static_cast<const char*>(bodyPtr), bodySize));
    }

    void HttpServer::AppendParamsToResponse(const char* method, protocol::HttpResponse* resp,
                                            const ParamMap& queryParams, const ParamMap& bodyParams) {
        std::ostringstream responseBody;
        responseBody << "Hello from qifeng HTTP server!";
        if (!queryParams.empty()) {
            responseBody << "\nquery params:";
            for (const auto& kv : queryParams) {
                SLOG_DEBUG << method << " query param: " << kv.first << "=" << kv.second;
                responseBody << "\n" << kv.first << "=" << kv.second;
            }
        }
        if (!bodyParams.empty()) {
            responseBody << "\nbody params:";
            for (const auto& kv : bodyParams) {
                SLOG_DEBUG << method << " body param: " << kv.first << "=" << kv.second;
                responseBody << "\n" << kv.first << "=" << kv.second;
            }
        }
        resp->set_status_code("200");
        resp->append_output_body(responseBody.str());
    }

    void HttpServer::HandleUnknownMethod(const char* method, protocol::HttpResponse* resp) {
        SLOG_WARN << "Unknown HTTP method: " << (method == nullptr ? "<null>" : method) << ", fallback to UNKNOWN";
        resp->set_status_code("400");
        resp->append_output_body("Unknown HTTP method");
    }

    void HttpServer::Process(WFHttpTask* server_task) {
        protocol::HttpRequest* req = server_task->get_req();
        protocol::HttpResponse* resp = server_task->get_resp();
        const char* method = req->get_method();  // GET/POST/PUT/DELETE
        const char* uri = req->get_request_uri();
        // 把uri和method写入日志
        SLOG_DEBUG << "Received HTTP request: method=" << method << ", uri=" << uri;
        // 路由分发
        if (method == nullptr || method[0] == '\0') {
            HandleUnknownMethod(method, resp);
            return;
        }
        const std::string methodStr(method);
        const HttpMethod httpMethod = Http::StringToHttpMethod(methodStr);  // 打印警告日志（如果有的话）
        if (httpMethod == HttpMethod::UNKNOWN) {
            HandleUnknownMethod(method, resp);
            SLOG_ERROR << "Unsupported HTTP method: " << methodStr;
            return;
        }
        const ParamMap queryParams = ParseQueryParamsFromUri(uri);
        const ParamMap bodyParams = ParseBodyParams(req);
        switch (httpMethod) {
            case HttpMethod::GET:  // GET
                AppendParamsToResponse("GET", resp, queryParams, {});
                break;
            case HttpMethod::POST:  // POST
                AppendParamsToResponse("POST", resp, queryParams, bodyParams);
                break;
            case HttpMethod::PUT:  // PUT
                AppendParamsToResponse("PUT", resp, queryParams, bodyParams);
                break;
            case HttpMethod::PATCH:  // PATCH
                AppendParamsToResponse("PATCH", resp, queryParams, bodyParams);
                break;
            case HttpMethod::DELETE:  // DELETE
                AppendParamsToResponse("DELETE", resp, queryParams, bodyParams);
                break;
            case HttpMethod::HEAD:  // HEAD
                AppendParamsToResponse("HEAD", resp, queryParams, bodyParams);
                break;
            case HttpMethod::OPTIONS:  // OPTIONS
                AppendParamsToResponse("OPTIONS", resp, queryParams, bodyParams);
                break;
            case HttpMethod::UNKNOWN:  // UNKNOWN
                HandleUnknownMethod(method, resp);
                break;
            default:
                HandleUnknownMethod(method, resp);
        }
    }

    bool HttpServer::StartServer(unsigned short port, SeverProcess process) {
        struct WFServerParams params = HTTP_SERVER_PARAMS_DEFAULT;
        params.request_size_limit = static_cast<size_t>(8) * static_cast<size_t>(1024) * static_cast<size_t>(1024);
        WFHttpServer server(&params, process);
        if (server.start(port) == 0) {
            pause();
            server.stop();
        } else {
            SLOG_ERROR << "Failed to start HTTP server on port " << port;
            return false;
        }
        return true;
    }

}  // namespace qifeng
