/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
// OpenAI API客户端实现，优化为更接近Python的调用方式

#ifndef QIFENG_FRAMEWORK_INCLUDE_HTTP_OPENAI_CLIENT_H
#define QIFENG_FRAMEWORK_INCLUDE_HTTP_OPENAI_CLIENT_H

#include <map>
#include <string>

#include "http/http.h"

namespace qifeng {

    class OpenAIClient : public Http {
    public:
        // 设置基础URL
        void SetBaseUrl(const std::string& base_url);

        // 设置默认请求头
        void SetDefaultHeader(const std::string& name, const std::string& value);
        void RemoveDefaultHeader(const std::string& name);
        void ClearDefaultHeaders();

        // 设置代理
        void SetProxy(const std::string& proxy_url);

        // 设置SSL验证
        void SetSSLVerification(bool verify);

        // 设置超时时间
        void SetTimeout(int seconds);

        // HTTP请求方法
        HttpResponse Get(const HttpRequest& config);

        HttpResponse Post(const HttpRequest& config);

        HttpResponse Put(const HttpRequest& config);

        HttpResponse Delete(const HttpRequest& config);

        // 通用请求方法
        HttpResponse Request(const HttpRequest& config);

        // SSE请求方法
        HttpResponse RequestSSE(const HttpRequest& config, const HttpCallback& callback);

        // URL编码
        std::string UrlEncode(const std::string& text);

        // URL解码
        std::string UrlDecode(const std::string& text);

        // 构建完整URL（拼接基础URL和路径）
        std::string BuildUrl(const std::string& url, const std::map<std::string, std::string>& params);

        OpenAIClient();
        ~OpenAIClient();

        OpenAIClient(const OpenAIClient&) = delete;
        OpenAIClient& operator=(const OpenAIClient&) = delete;
        OpenAIClient(OpenAIClient&&) = delete;
        OpenAIClient& operator=(OpenAIClient&&) = delete;

    private:
        std::string mBaseUrl;                                // 基础URL
        std::map<std::string, std::string> mDefaultHeaders;  // 默认请求头
        std::string mProxy;                                  // 代理服务器
        bool mVerifySSl;                                     // 是否验证SSL证书
        int mTimeout;                                        // 超时时间（秒）
        bool mIsAsync;                                       // 同步请求还是异步请求
    };

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_INCLUDE_HTTP_OPENAI_CLIENT_H