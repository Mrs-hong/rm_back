/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_HTTP_HTTP_H
#define QIFENG_FRAMEWORK_INCLUDE_HTTP_HTTP_H

#include <functional>
#include <string>
#include <unordered_map>
#include <workflow/WFFacilities.h>
#include <workflow/WFHttpServer.h>

// 封装HTTP请求/响应、客户端、服务端等通用能力
// 依赖workflow库

namespace qifeng {
    enum class HttpMethod { GET, POST, PUT, DELETE, HEAD, PATCH, OPTIONS, UNKNOWN };

    // HTTP响应结构体
    struct HttpResponse {
        int mStatusCode = 0;                                    // HTTP状态码
        std::string mBody;                                      // 响应体内容
        std::unordered_map<std::string, std::string> mHeaders;  // 响应头
        bool mIsError = false;                                  // 是否为错误
        std::string mErrorMessage;                              // 错误信息
    };

    // HTTP请求结构体
    struct HttpRequest {
        std::string mUrl;                                       // 请求URL
        HttpMethod mMethod = HttpMethod::GET;                   // 请求方法
        std::string mBody;                                      // 请求体内容
        std::string mProxy;                                     // 代理服务器地址
        std::unordered_map<std::string, std::string> mHeaders;  // 请求头
        int mTimeoutMs = 60000;                                 // 超时时间（毫秒）
        bool mVerifySSL = true;                                 // 是否验证SSL证书
        bool mStream = false;                                   // 是否采用流式响应
    };

    // HTTP异步请求回调类型
    using HttpCallback = std::function<void(const HttpResponse& rsp)>;

    class Http {
    public:
        // 枚举转字符串，未知类型返回GET
        static std::string HttpMethodToString(HttpMethod method);
        // 字符串转枚举，未知类型返回HttpMethod::UNKNOWN
        static HttpMethod StringToHttpMethod(const std::string& method_str);
        // 处理HTTP客户端异步请求完成的静态回调实现
        static void OnHttpTaskFinished(WFHttpTask* task, HttpCallback cb);

        // 发送HTTP请求（异步，回调）
        // @param req  请求参数
        // @param cb   响应回调
        static void AsyncRequest(const HttpRequest& req, HttpCallback cb);

        // 发送HTTP请求（同步，阻塞，返回响应）
        // @param req  请求参数
        // @return     响应结果
        static HttpResponse SyncRequest(const HttpRequest& req);

    protected:
        // HTTP请求默认最大重定向次数
        static constexpr int DefaultRedirectMax = 10;
        // HTTP请求默认最大重试次数
        static constexpr int DefaultRetryMax = 10;
        // HTTP请求默认超时时间（毫秒）
        static constexpr int DefaultTimeoutMs = 60000;
        // multipart上传默认分隔符
        static constexpr const char* DefaultBoundary = "----qifenghttpboundary";
    };

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_INCLUDE_HTTP_HTTP_H
