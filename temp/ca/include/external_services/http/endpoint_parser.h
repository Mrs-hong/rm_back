//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_EXTERNAL_SERVICES_HTTP_ENDPOINT_PARSER_H
#define QIFENG_CA_EXTERNAL_SERVICES_HTTP_ENDPOINT_PARSER_H

#include <cstdint>
#include <string>

namespace qifeng_ca::external_services {

    // 是否使用 https
    inline bool UseSsl(const std::string &endpoint) {
        return endpoint.rfind("https://", 0) == 0;
    }

    // 去掉 scheme 后的主机部分(到冒号或斜杠前)
    inline std::string ParseHost(const std::string &endpoint) {
        size_t pos = 0;
        if (endpoint.rfind("http://", 0) == 0) {
            pos = 7;
        } else if (endpoint.rfind("https://", 0) == 0) {
            pos = 8;
        }
        size_t colon = endpoint.find(':', pos);
        size_t slash = endpoint.find('/', pos);
        size_t end = std::string::npos;
        if (colon != std::string::npos && (slash == std::string::npos || colon < slash)) {
            end = colon;
        } else if (slash != std::string::npos) {
            end = slash;
        }
        return endpoint.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    }

    // 解析端口(无显式端口时 https=>443, http=>80)
    inline uint16_t ParsePort(const std::string &endpoint) {
        size_t pos = 0;
        if (endpoint.rfind("http://", 0) == 0) {
            pos = 7;
        } else if (endpoint.rfind("https://", 0) == 0) {
            pos = 8;
        }
        size_t colon = endpoint.find(':', pos);
        if (colon == std::string::npos) {
            return UseSsl(endpoint) ? 443 : 80;
        }
        size_t slash = endpoint.find('/', colon + 1);
        std::string portStr =
            endpoint.substr(colon + 1, slash == std::string::npos ? std::string::npos : slash - colon - 1);
        try {
            return static_cast<uint16_t>(std::stoi(portStr));
        } catch (...) {
            return UseSsl(endpoint) ? 443 : 80;
        }
    }

    // 解析 path 部分(第一个 '/' 之后), 默认 "/"
    inline std::string ParsePath(const std::string &endpoint) {
        size_t pos = 0;
        if (endpoint.rfind("http://", 0) == 0) {
            pos = 7;
        } else if (endpoint.rfind("https://", 0) == 0) {
            pos = 8;
        }
        size_t slash = endpoint.find('/', pos);
        return slash == std::string::npos ? "/" : endpoint.substr(slash);
    }

}  // namespace qifeng_ca::external_services

#endif  // QIFENG_CA_EXTERNAL_SERVICES_HTTP_ENDPOINT_PARSER_H
