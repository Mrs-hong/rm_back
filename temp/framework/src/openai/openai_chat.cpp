/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
// OpenAI API客户端实现，优化为更接近Python的调用方式

#include <cstdio>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "common/config_manager.h"
#include "common/logger.h"
#include "curl/curl_http_client.h"
#include "curl/sse_parser.h"
#include "http/http.h"
#include "json/reader.h"
#include "json/writer.h"
#include "openai/openai_chat.h"
#include "openai/openai_client.h"

namespace qifeng {

    // 辅助函数：从配置或直接参数获取值
    std::string OpenAIChat::GetConfigOrParam(const std::string& config_key, const std::string& param_value,
                                             const std::string& default_value) {
        if (!param_value.empty()) {
            return param_value;
        }

        try {
            return ConfigManager::GetInstance().GetString("openai", config_key, default_value);
        } catch (const std::exception& e) {
            SLOG_DEBUG << "Failed to get config " << config_key << ": " << e.what()
                       << ", using default: " << default_value;
            return default_value;
        }
    }

    // 辅助函数实现：SSE请求体构建
    std::string OpenAIChat::BuildRequestBody(const RequestContext& ctx) {
        if (ctx.mMessages != Json::nullValue) {
            std::string bodyStr;
            Json::StreamWriterBuilder writerBuilder;
            Json::Value bodyJson;
            bodyJson["model"] = ctx.mModel;
            bodyJson["messages"] = ctx.mMessages;
            bodyJson["stream"] = ctx.mRequest.mStream;
            bodyJson["temperature"] = ctx.mTemperature;
            bodyStr = Json::writeString(writerBuilder, bodyJson);
            return bodyStr;
        } else {
            SLOG_ERROR << "Request messages are null, cannot build request body";
            return ctx.mRequest.mBody;
        }
    }

    // 辅助函数实现：SSE请求日志
    void OpenAIChat::LogSSERequest(const RequestContext& ctx, const std::string& bodyStr) {
        std::stringstream headersSs;
        for (const auto& [key, value] : ctx.mRequest.mHeaders) {
            if (key != "Authorization") {
                headersSs << key << ": " << value << "; ";
            } else {
                headersSs << key << ": Bearer [REDACTED]; ";
            }
        }
        SLOG_DEBUG << "[OpenAIClient] SSE Request headers: " << headersSs.str();
        if (!bodyStr.empty()) {
            SLOG_DEBUG << "[OpenAIClient] SSE Request body: " << bodyStr;
        }
    }

    // OpenAIChat类内静态私有方法实现
    void OpenAIChat::BuildRequestHeaders(RequestContext& ctx) {
        std::unordered_map<std::string, std::string>& requestHeaders = ctx.mRequest.mHeaders;
        if (!ctx.mApiKey.empty()) {
            requestHeaders["Authorization"] = "Bearer " + ctx.mApiKey;
        }
        if (!ctx.mOrganization.empty()) {
            requestHeaders["OpenAI-Organization"] = ctx.mOrganization;
        }
        if (!ctx.mMessages.isNull() && requestHeaders.find("Content-Type") == requestHeaders.end()) {
            requestHeaders["Content-Type"] = "application/json";
        }
    }

    // 辅助方法：去除chunked编码，返回纯净body
    std::string OpenAIChat::RemoveChunkedEncoding(const std::string& chunked_body) {
        // 直接提取第一个'{'到最后一个'}'之间的内容
        size_t left = chunked_body.find('{');
        size_t right = chunked_body.rfind('}');
        if (left != std::string::npos && right != std::string::npos && right > left) {
            return chunked_body.substr(left, right - left + 1);
        }
        return chunked_body;
    }

    void OpenAIChat::LogRequest(const RequestContext& ctx) {
        std::stringstream headersSs;
        for (const auto& [key, value] : ctx.mRequest.mHeaders) {
            if (key != "Authorization") {
                headersSs << key << ": " << value << "; ";
            } else {
                headersSs << key << ": Bearer [REDACTED]; ";
            }
        }
        SLOG_DEBUG << "[OpenAIClient] Request headers: " << headersSs.str();
        if (ctx.mMessages != Json::nullValue) {
            Json::StreamWriterBuilder writerBuilder;
            writerBuilder["indentation"] = "";
            std::string bodyStr = Json::writeString(writerBuilder, ctx.mMessages);
            SLOG_DEBUG << "[OpenAIClient] Request body: " << bodyStr;
        }
    }

    void OpenAIChat::LogResponse(const HttpResponse& response) {
        SLOG_DEBUG << "[OpenAIClient] Response status code: " << response.mStatusCode;
        std::stringstream responseHeadersSs;
        for (const auto& [key, value] : response.mHeaders) {
            responseHeadersSs << key << ": " << value << "; ";
        }
        SLOG_DEBUG << "[OpenAIClient] Response headers: " << responseHeadersSs.str();
        SLOG_DEBUG << "[OpenAIClient] Response body: " << response.mBody;
    }

    HttpResponse OpenAIChat::DoRequest(RequestContext& ctx) {
        if (ctx.mRequest.mMethod == HttpMethod::GET) {
            return ctx.mClient->Get(ctx.mRequest);
        } else if (ctx.mRequest.mMethod == HttpMethod::POST) {
            std::string bodyStr;
            if (ctx.mMessages != Json::nullValue) {
                Json::StreamWriterBuilder writerBuilder;
                Json::Value bodyJson;
                bodyJson["model"] = ctx.mModel;
                bodyJson["messages"] = ctx.mMessages;
                bodyJson["stream"] = ctx.mRequest.mStream;
                bodyJson["temperature"] = ctx.mTemperature;
                bodyStr = Json::writeString(writerBuilder, bodyJson);
            }
            ctx.mRequest.mBody = bodyStr;
            return ctx.mClient->Post(ctx.mRequest);
        } else if (ctx.mRequest.mMethod == HttpMethod::DELETE) {
            return ctx.mClient->Delete(ctx.mRequest);
        } else {
            throw std::runtime_error("Unsupported HTTP method: " +
                                     std::to_string(static_cast<int>(ctx.mRequest.mMethod)));
        }
    }

    OpenAIChat::OpenAIChat(const std::string& api_key, const std::string& base_url, const std::string& organization,
                           bool throw_exception)
        : mApiKey(GetConfigOrParam("api_key", api_key, "")),
          mOrganization(GetConfigOrParam("organization", organization, "")),

          mBaseUrl(GetConfigOrParam("base_url", base_url, "https://api.openai.com/v1/")),
          mThrowException(throw_exception) {
        mOpenAIClient = std::make_unique<OpenAIClient>();

        // 确保base_url_始终以斜杠结尾
        if (!mBaseUrl.empty() && mBaseUrl.back() != '/') {
            mBaseUrl += '/';
        }

        // 从配置获取代理和超时设置
        std::string proxy = ConfigManager::GetInstance().GetString("openai", "proxy", "");
        if (!proxy.empty()) {
            SetProxy(proxy);
        }

        int timeout = ConfigManager::GetInstance().GetInt("openai", "timeout", 30);
        SetTimeout(timeout);

        SLOG_DEBUG << "OpenAIClient initialized with base URL: " << mBaseUrl
                   << ", API Key: " << (mApiKey.empty() ? "[NOT SET]" : "[SET]")
                   << ", Organization: " << (mOrganization.empty() ? "[NOT SET]" : mOrganization)
                   << ", Throw Exception: " << std::boolalpha << mThrowException;
    }

    OpenAIChat::~OpenAIChat() {
    }

    void OpenAIChat::SetApiBaseUrl(const std::string& api_base_url) {
        mBaseUrl = api_base_url;
    }

    void OpenAIChat::SetProxy(const std::string& proxy) {
        mOpenAIClient->SetProxy(proxy);
    }

    void OpenAIChat::SetSSLVerification(bool verify) {
        mOpenAIClient->SetSSLVerification(verify);
    }

    void OpenAIChat::SetTimeout(int seconds) {
        mOpenAIClient->SetTimeout(seconds);
    }

    Json::Value OpenAIChat::SendRequest(RequestContext& ctx) {
        if (ConfigManager::GetInstance().GetBool("log", "debug_mode", true)) {
            LogRequest(ctx);
        }

        HttpResponse response = DoRequest(ctx);
        if (ConfigManager::GetInstance().GetBool("log", "debug_mode", true)) {
            LogResponse(response);
        }
        // 去除chunked编码（如果存在），得到纯净的body内容
        response.mBody = RemoveChunkedEncoding(response.mBody);
        Json::Value result = ParseResponse(response);
        CheckResponse(result);
        return result;
    }

    Json::Value OpenAIChat::ParseResponse(const HttpResponse& response) {
        Json::Value result;

        if (response.mStatusCode == 200) {
            // 尝试解析JSON响应
            Json::Reader reader;
            if (reader.parse(response.mBody, result)) {
                return result;
            }

            // 如果解析失败，将响应体作为字符串返回
            result["text"] = response.mBody;
        } else {
            // 处理错误响应
            result["error"] = Json::objectValue;
            result["error"]["code"] = response.mStatusCode;
            result["error"]["message"] = response.mBody;
        }
        result["code"] = response.mStatusCode;
        return result;
    }

    void OpenAIChat::CheckResponse(const Json::Value& json) {
        if (json.isObject() && json.isMember("error")) {
            std::string errorMsg = "API Error: ";
            if (json["error"].isObject() && json["error"].isMember("message")) {
                errorMsg += json["error"]["message"].asString();
            } else {
                errorMsg += "Unknown error";
            }

            if (json["error"].isObject() && json["error"].isMember("code")) {
                errorMsg += " (Code: " + std::to_string(json["error"]["code"].asInt()) + ")";
            }

            TriggerError(errorMsg);
        }
    }

    void OpenAIChat::TriggerError(const std::string& msg) {
        if (mThrowException) {
            throw std::runtime_error(msg);
        } else {
            SLOG_ERROR << "OpenAIChat Error: " << msg;
        }
    }

    void OpenAIChat::ChatCompletionsCreateStream(const HttpRequest& req, Json::Value messages) {
        auto& cfg = ConfigManager::GetInstance();

        // 构建请求体
        Json::Value requestBody;
        requestBody["model"] = cfg.GetString("openai", "model", "deepseek-chat");
        requestBody["messages"] = messages;
        // 启用流式
        requestBody["stream"] = true;
        requestBody["temperature"] = cfg.GetDouble("openai", "temperature");

        // 构建 headers
        std::map<std::string, std::string> headers;
        if (!mApiKey.empty()) {
            headers["Authorization"] = "Bearer " + mApiKey;
        }
        if (!mOrganization.empty()) {
            headers["OpenAI-Organization"] = mOrganization;
        }
        headers["Content-Type"] = "application/json";
        headers["Accept"] = "text/event-stream";
        headers["Cache-Control"] = "no-cache";
        headers["Connection"] = "keep-alive";

        // 完整 URL
        std::string fullUrl = mBaseUrl + req.mUrl;

        CurlHttpClient client;
        SSEParser parser(
            [](const Json::Value& data) {
                // 每个完整的 SSE event
                std::string content = data["choices"][0]["delta"]["content"].asString();
                std::cout << content << std::flush;
            },
            []() { std::cout << "\n[Stream End]" << std::endl; });

        auto onChunk = [&parser](const std::string& chunk) { parser.Feed(chunk); };

        client.PostStream(fullUrl, requestBody.toStyledString(), headers, onChunk);
    }

    Json::Value OpenAIChat::ChatCompletionsCreate(const HttpRequest& req, Json::Value messages) {
        auto& cfg = ConfigManager::GetInstance();
        // 构建RequestContext
        RequestContext ctx;
        ctx.mRequest = req;
        ctx.mModel = cfg.GetString("openai", "model", "deepseek-chat");
        ctx.mMessages = messages;
        ctx.mApiKey = mApiKey;
        ctx.mOrganization = mOrganization;
        ctx.mBaseUrl = mBaseUrl;
        ctx.mRequest.mUrl = ctx.mBaseUrl + req.mUrl;
        ctx.mClient = mOpenAIClient.get();
        BuildRequestHeaders(ctx);
        return SendRequest(ctx);
    }
}  // namespace qifeng
