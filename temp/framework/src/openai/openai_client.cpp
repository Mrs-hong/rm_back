/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
// 基于libcurl的HTTP客户端实现

#include <cctype>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "common/logger.h"
#include "openai/openai_client.h"

namespace qifeng {

    OpenAIClient::OpenAIClient() {
        mVerifySSl = false;
        mTimeout = 30000;
        mIsAsync = false;
    }

    OpenAIClient::~OpenAIClient() {
    }

    void OpenAIClient::SetBaseUrl(const std::string& base_url) {
        mBaseUrl = base_url;
        // 确保mBaseUrl以/结尾
        if (!mBaseUrl.empty() && mBaseUrl.back() != '/') {
            mBaseUrl.append("/");
        } else {
            SLOG_DEBUG << "Base URL set to: " << mBaseUrl;
        }
    }

    void OpenAIClient::SetDefaultHeader(const std::string& name, const std::string& value) {
        mDefaultHeaders[name] = value;
    }

    void OpenAIClient::RemoveDefaultHeader(const std::string& name) {
        auto it = mDefaultHeaders.find(name);
        if (it != mDefaultHeaders.end()) {
            mDefaultHeaders.erase(it);
        } else {
            SLOG_ERROR << "Header not found: " << name;
        }
    }

    void OpenAIClient::ClearDefaultHeaders() {
        mDefaultHeaders.clear();
    }

    void OpenAIClient::SetProxy(const std::string& proxy_url) {
        mProxy = proxy_url;
    }

    void OpenAIClient::SetSSLVerification(bool verify) {
        mVerifySSl = verify;
    }

    void OpenAIClient::SetTimeout(int seconds) {
        mTimeout = seconds;
    }
    std::string OpenAIClient::UrlEncode(const std::string& text) {
        std::ostringstream oss;
        oss << std::hex << std::uppercase;
        for (char c : text) {
            // RFC 3986 unreserved characters
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                c == '.' || c == '~') {
                oss << c;
            } else {
                oss << '%' << std::setw(2) << std::setfill('0') << int(static_cast<unsigned char>(c));
            }
        }
        return oss.str();
    }
    std::string OpenAIClient::UrlDecode(const std::string& text) {
        std::ostringstream oss;
        size_t i = 0;
        while (i < text.length()) {
            if (text[i] == '%' && i + 2 < text.length() && std::isxdigit(text[i + 1]) && std::isxdigit(text[i + 2])) {
                // 解析%XX
                std::string hex = text.substr(i + 1, 2);
                char decodedChar = static_cast<char>(std::stoi(hex, nullptr, 16));
                oss << decodedChar;
                i += 3;
            } else if (text[i] == '+') {
                // 按照application/x-www-form-urlencoded规范，+解码为空格
                oss << ' ';
                i++;
            } else {
                oss << text[i];
                i++;
            }
        }
        return oss.str();
    }

    std::string OpenAIClient::BuildUrl(const std::string& url, const std::map<std::string, std::string>& params) {
        std::ostringstream fullUrl;

        // 拼接基础URL和路径
        if (url.find("http://") == 0 || url.find("https://") == 0) {
            // 如果url已经是完整的URL，则直接使用
            fullUrl << url;
        } else {
            // 否则拼接基础URL
            if (url.empty() || url.front() == '/') {
                fullUrl << mBaseUrl << (url.empty() ? "" : url.substr(1));
            } else {
                fullUrl << mBaseUrl << url;
            }
        }

        // 添加查询参数
        if (!params.empty()) {
            char separator = '?';
            for (const auto& [key, value] : params) {
                fullUrl << separator << UrlEncode(key) << "=" << UrlEncode(value);
                separator = '&';
            }
        }

        return fullUrl.str();
    }

    HttpResponse OpenAIClient::Get(const HttpRequest& config) {
        return SyncRequest(config);
    }

    HttpResponse OpenAIClient::Post(const HttpRequest& config) {
        return SyncRequest(config);
    }

    HttpResponse OpenAIClient::Put(const HttpRequest& config) {
        return SyncRequest(config);
    }

    HttpResponse OpenAIClient::Delete(const HttpRequest& config) {
        return SyncRequest(config);
    }

}  // namespace qifeng