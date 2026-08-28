/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
// OpenAI API客户端实现，优化为更接近Python的调用方式

#ifndef QIFENG_FRAMEWORK_INCLUDE_HTTP_OPENAI_CHAT_H
#define QIFENG_FRAMEWORK_INCLUDE_HTTP_OPENAI_CHAT_H

#include <string>

#include "http/http.h"
#include "json/value.h"
#include "openai_client.h"

namespace qifeng {
    // SSE消息回调函数类型

    class OpenAIChat {
    public:
        struct Tools {
            Json::Value mTool;
            std::string mToolChoice;
        };
        // 请求上下文结构体
        struct RequestContext {
            HttpRequest mRequest;
            std::string mModel;
            Json::Value mMessages;
            std::string mApiKey;
            std::string mOrganization;
            std::string mBaseUrl;
            HttpCallback mCallback;
            Tools mTools;
            double mTemperature = 0.7;
            int mMaxTokens = 10000;
            OpenAIClient* mClient = nullptr;
        };

        // 工具调用结构体
        struct ToolCall {
            std::string mId;            // 工具调用ID
            std::string mType;          // 调用类型（目前只有function）
            std::string mFunctionName;  // 函数名称
            Json::Value mParameters;    // 函数参数（已解析为JSON）
        };

    private:
        static void BuildRequestHeaders(RequestContext& ctx);
        static void LogRequest(const RequestContext& ctx);
        static void LogResponse(const HttpResponse& response);
        static HttpResponse DoRequest(RequestContext& ctx);

        std::string BuildRequestBody(const RequestContext& ctx);
        void LogSSERequest(const RequestContext& ctx, const std::string& bodyStr);
        HttpCallback WrapSSECallback(const HttpCallback& callback);

    public:
        // 解析带有chunked编码的响应体，返回纯净的body内容
        static std::string RemoveChunkedEncoding(const std::string& chunked_body);

        // 从JSON响应中提取content内容
        std::string GetConfigOrParam(const std::string& config_key, const std::string& param_value,
                                     const std::string& default_value);

        // 构造函数，支持从配置或直接参数初始化
        explicit OpenAIChat(const std::string& api_key = "", const std::string& base_url = "",
                            const std::string& organization = "", bool throw_exception = true);

        // 析构函数
        ~OpenAIChat();

        // 设置API密钥
        void SetApiKey(const std::string& api_key);

        // 设置组织ID
        void SetOrganization(const std::string& organization);

        // 设置API基础URL
        void SetApiBaseUrl(const std::string& api_base_url);

        // 设置代理
        void SetProxy(const std::string& proxy);

        // 设置SSL验证
        void SetSSLVerification(bool verify);

        // 设置超时时间
        void SetTimeout(int seconds);

    public:
        // 以下是直接暴露的便捷方法，内部调用上述接口实现
        // 直接创建聊天完成（便捷方法）,返回完整响应
        Json::Value ChatCompletionsCreate(const HttpRequest& req, Json::Value messages);

        // 使用 libcurl 的流式聊天
        void ChatCompletionsCreateStream(const HttpRequest& req, Json::Value messages);

    private:
        // 发送请求的内部方法
        Json::Value SendRequest(RequestContext& ctx);

        // JSON响应解析
        Json::Value ParseResponse(const HttpResponse& response);

        // 检查响应是否有错误
        void CheckResponse(const Json::Value& json);

        // 触发错误处理
        void TriggerError(const std::string& msg);

        // 禁用拷贝和移动
        OpenAIChat(const OpenAIChat&) = delete;
        OpenAIChat& operator=(const OpenAIChat&) = delete;
        OpenAIChat(OpenAIChat&&) = delete;
        OpenAIChat& operator=(OpenAIChat&&) = delete;

    private:
        // 成员变量
        std::unique_ptr<OpenAIClient> mOpenAIClient;
        std::string mApiKey;
        std::string mOrganization;
        std::string mBaseUrl;
        bool mThrowException;
    };

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_INCLUDE_HTTP_OPENAI_CHAT_H
