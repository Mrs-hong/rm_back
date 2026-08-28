/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_HTTP_HTTP_SERVER_H
#define QIFENG_FRAMEWORK_INCLUDE_HTTP_HTTP_SERVER_H

#include <functional>
#include <workflow/WFFacilities.h>
#include <workflow/WFHttpServer.h>

#include "http.h"

// 封装HTTP请求/响应、客户端、服务端等通用能力
// 依赖workflow库

namespace qifeng {

    using SeverProcess = std::function<void(WFHttpTask* proxy_task)>;
    using ParamMap = std::unordered_map<std::string, std::string>;
    class HttpServer : public Http {
        // 辅助参数解析与响应方法
        /**
         * 解析类似 "a=1&b=2" 形式的参数字符串为键值对map
         * @param source 形如 a=1&b=2 的字符串
         * @return      解析后的参数map
         */
        static ParamMap ParseKvParams(const std::string& source);

        /**
         * 从URI中解析查询参数部分（?后面的内容）为map
         * @param uri   完整URI字符串
         * @return      查询参数map
         */
        static ParamMap ParseQueryParamsFromUri(const char* uri);

        /**
         * 从HttpRequest对象中解析body参数为map（仅支持x-www-form-urlencoded）
         * @param req   HTTP请求对象
         * @return      body参数map
         */
        static ParamMap ParseBodyParams(protocol::HttpRequest* req);

        /**
         * 将解析到的参数信息写入HTTP响应体，并设置状态码200
         * @param method      HTTP方法名（如GET/POST）
         * @param resp        HTTP响应对象
         * @param queryParams 查询参数map
         * @param bodyParams  body参数map
         */
        static void AppendParamsToResponse(const char* method, protocol::HttpResponse* resp,
                                           const ParamMap& queryParams, const ParamMap& bodyParams);

        /**
         * 处理未知HTTP方法，设置响应状态码400并写入提示
         * @param method HTTP方法名
         * @param resp   HTTP响应对象
         */
        static void HandleUnknownMethod(const char* method, protocol::HttpResponse* resp);

    public:
        // 启动HTTP服务端，监听端口，回调处理请求
        // @param port     监听端口
        // @param handler  请求处理回调
        // @return         启动是否成功
        static bool StartServer(unsigned short port, SeverProcess process);

        // 处理服务端收到的HTTP请求的静态回调实现
        static void Process(WFHttpTask* server_task);
    };

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_INCLUDE_HTTP_HTTP_SERVER_H