/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_CURL_CURL_HTTP_CLIENT_H
#define QIFENG_FRAMEWORK_INCLUDE_CURL_CURL_HTTP_CLIENT_H
#include <curl/curl.h>
#include <functional>
#include <map>
#include <string>

namespace qifeng {
    using StreamChunkCallback = std::function<void(const std::string& chunk)>;

    class CurlHttpClient {
    public:
        CurlHttpClient();
        CurlHttpClient(const CurlHttpClient&) = delete;
        CurlHttpClient& operator=(const CurlHttpClient&) = delete;
        CurlHttpClient(CurlHttpClient&&) = delete;
        CurlHttpClient& operator=(CurlHttpClient&&) = delete;
        ~CurlHttpClient();

        // SSE 流式请求（使用持久化 parser）
        bool PostStream(const std::string& url, const std::string& body,
                        const std::map<std::string, std::string>& headers, StreamChunkCallback onChunk);

    private:
        static size_t StreamWriteCallback(void* contents, size_t size, size_t nmemb, void* userdata);

        CURL* mCurl;
    };

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_INCLUDE_CURL_CURL_HTTP_CLIENT_H