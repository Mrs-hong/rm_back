/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_HTTP_RESPONSE_H
#define QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_HTTP_RESPONSE_H

#include <string>

#include "common/utils/json_utils.h"
#include "drogon/HttpResponse.h"

namespace common {
    namespace utils {

        /**
         * @brief 通用HTTP响应结构体
         * 包含状态码、响应消息和响应数据三个成员
         */
        struct HttpResponse {
            int mCode {0};         ///< 业务状态码
            std::string mMessage;  ///< 响应消息
            Json::Value mData;     ///< 响应数据

            // 使用JsonUtils宏实现JSON转换功能
            BEGIN_JSON_PARSER
            ADD_MEMBER(mCode)
            ADD_MEMBER(mMessage)
            ADD_MEMBER(mData)
            END_JSON_PARSER

            /**
             * @brief 创建成功响应
             * @param message 响应消息
             * @param data 响应数据（可选）
             * @return HttpResponse 成功响应对象
             */
            static HttpResponse Success(const std::string& message = "Success",
                                        const Json::Value& data = Json::Value());

            /**
             * @brief 创建错误响应
             * @param code 错误状态码
             * @param message 错误消息
             * @param data 错误数据（可选）
             * @return HttpResponse 错误响应对象
             */
            static HttpResponse Error(int code, const std::string& message, const Json::Value& data = Json::Value());

            /**
             * @brief 转换为Drogon的HttpResponsePtr
             * @return drogon::HttpResponsePtr Drogon HTTP响应指针
             */
            drogon::HttpResponsePtr ToDrogonResponse() const;
        };

    }  // namespace utils
}  // namespace common

#endif  // QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_HTTP_RESPONSE_H
