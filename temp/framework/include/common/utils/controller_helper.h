/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_CONTROLLER_HELPER_H
#define QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_CONTROLLER_HELPER_H

#include <functional>
#include <string>
#include <utility>

#include "common/logger.h"
#include "common/utils/response_utils.h"
#include "drogon/HttpRequest.h"
#include "drogon/HttpResponse.h"

namespace common {
    namespace utils {

        /**
         * @brief 响应包装器
         * 封装Drogon的HttpResponsePtr，提供简化的响应生成API
         */
        class ResponseWrapper {
        public:
            ResponseWrapper() : mResponse(drogon::HttpResponse::newHttpResponse()) {
            }

            /**
             * @brief 设置响应状态码
             * @param code 状态码
             */
            void SetStatusCode(drogon::HttpStatusCode code) {
                mResponse->setStatusCode(code);
            }

            /**
             * @brief 设置响应内容类型
             * @param contentType 内容类型
             */
            void SetContentType(const std::string& contentType) {
                mResponse->setContentTypeString(contentType);
            }

            /**
             * @brief 设置响应内容类型编码
             * @param contentType 内容类型
             */
            void SetContentTypeCode(drogon::ContentType contentType) {
                mResponse->setContentTypeCode(contentType);
            }

            /**
             * @brief 设置响应体
             * @param body 响应体
             */
            void SetBody(const std::string& body) {
                mResponse->setBody(body);
            }

            /**
             * @brief 设置JSON响应体
             * @param json JSON值
             */
            void SetJson(const Json::Value& json) {
                mResponse = drogon::HttpResponse::newHttpJsonResponse(json);
            }

            /**
             * @brief 发送成功响应
             * @param message 响应消息
             * @param data 响应数据（可选）
             */
            void Success(const std::string& message = "Success", const Json::Value& data = Json::Value()) {
                auto httpResp = HttpResponse::Success(message, data);
                mResponse = httpResp.ToDrogonResponse();
            }

            /**
             * @brief 发送错误响应
             * @param code 错误状态码
             * @param message 错误消息
             * @param data 错误数据（可选）
             */
            void Error(int code, const std::string& message, const Json::Value& data = Json::Value()) {
                auto httpResp = HttpResponse::Error(code, message, data);
                mResponse = httpResp.ToDrogonResponse();
            }

            /**
             * @brief 发送文件响应
             * @param filePath 文件路径
             */
            void File(const std::string& filePath) {
                mResponse = drogon::HttpResponse::newFileResponse(filePath);
            }

            /**
             * @brief 设置Drogon响应
             * @param resp Drogon响应指针
             */
            void SetResponse(drogon::HttpResponsePtr resp) {
                mResponse = std::move(resp);
            }

            /**
             * @brief 获取Drogon响应
             * @return drogon::HttpResponsePtr Drogon响应指针
             */
            drogon::HttpResponsePtr GetResponse() const {
                return mResponse;
            }

        private:
            drogon::HttpResponsePtr mResponse;
        };

        /**
         * @brief 控制器辅助类
         * 封装callback调用和异常处理逻辑
         */
        class ControllerHelper {
        public:
            /**
             * @brief 执行控制器方法并处理异常
             * @tparam Func 方法类型
             * @param req HTTP请求指针
             * @param callback 回调函数
             * @param func 控制器方法，该方法应该接受HttpRequestPtr和ResponseWrapper作为参数
             */
            template <typename Func>
            static void Execute(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& callback, Func&& func) {
                try {
                    ResponseWrapper respWrapper;
                    func(req, respWrapper);
                    callback(respWrapper.GetResponse());
                } catch (const std::exception& e) {
                    SLOG_ERROR << "Controller method exception: " << e.what();
                    callback(HttpResponse::Error(500, "Internal server error").ToDrogonResponse());
                } catch (...) {
                    SLOG_ERROR << "Unknown exception in controller method";
                    callback(HttpResponse::Error(500, "Internal server error").ToDrogonResponse());
                }
            }
        };

    }  // namespace utils
}  // namespace common

/**
 * @brief 控制器方法声明宏
 * 简化控制器方法的声明
 * @param method_name 方法名
 * @param ... 额外参数类型和名称
 */
#define CONTROLLER_METHOD(method_name, ...)                                                          \
    void method_name(const drogon::HttpRequestPtr& req,                                              \
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback, ##__VA_ARGS__); \
    void method_name##Impl(const drogon::HttpRequestPtr& req, common::utils::ResponseWrapper& resp, ##__VA_ARGS__)

/**
 * @brief 控制器方法实现宏
 * 简化控制器方法的实现
 * @param class_name 类名
 * @param method_name 方法名
 * @param ... 额外参数类型和名称
 */
// 无额外参数的方法宏
#define IMPLEMENT_CONTROLLER_METHOD(class_name, method_name)                                       \
    void class_name::method_name(const drogon::HttpRequestPtr& req,                                \
                                 std::function<void(const drogon::HttpResponsePtr&)>&& callback) { \
        common::utils::ControllerHelper::Execute(                                                  \
            req, std::move(callback),                                                              \
            [this](const drogon::HttpRequestPtr& req_, common::utils::ResponseWrapper& resp_) {    \
                this->method_name##Impl(req_, resp_);                                              \
            });                                                                                    \
    }                                                                                              \
    void class_name::method_name##Impl(const drogon::HttpRequestPtr& req, common::utils::ResponseWrapper& resp)

// 带一个uint64_t参数的方法宏
#define IMPLEMENT_CONTROLLER_METHOD_U64(class_name, method_name, id)                                            \
    void class_name::method_name(const drogon::HttpRequestPtr& req,                                             \
                                 std::function<void(const drogon::HttpResponsePtr&)>&& callback, uint64_t id) { \
        common::utils::ControllerHelper::Execute(                                                               \
            req, std::move(callback),                                                                           \
            [this, id](const drogon::HttpRequestPtr& req_, common::utils::ResponseWrapper& resp_) {             \
                this->method_name##Impl(req_, resp_, id);                                                       \
            });                                                                                                 \
    }                                                                                                           \
    void class_name::method_name##Impl(const drogon::HttpRequestPtr& req, common::utils::ResponseWrapper& resp, \
                                       uint64_t id)

// 带一个int64_t参数的方法宏
#define IMPLEMENT_CONTROLLER_METHOD_I64(class_name, method_name, id)                                            \
    void class_name::method_name(const drogon::HttpRequestPtr& req,                                             \
                                 std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id) {  \
        common::utils::ControllerHelper::Execute(                                                               \
            req, std::move(callback),                                                                           \
            [this, id](const drogon::HttpRequestPtr& req_, common::utils::ResponseWrapper& resp_) {             \
                this->method_name##Impl(req_, resp_, id);                                                       \
            });                                                                                                 \
    }                                                                                                           \
    void class_name::method_name##Impl(const drogon::HttpRequestPtr& req, common::utils::ResponseWrapper& resp, \
                                       int64_t id)

// 带两个int64_t参数的方法宏
#define IMPLEMENT_CONTROLLER_METHOD_I64_I64(class_name, method_name, id1, id2)                                  \
    void class_name::method_name(const drogon::HttpRequestPtr& req,                                             \
                                 std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id1,   \
                                 int64_t id2) {                                                                 \
        common::utils::ControllerHelper::Execute(                                                               \
            req, std::move(callback),                                                                           \
            [this, id1, id2](const drogon::HttpRequestPtr& req_, common::utils::ResponseWrapper& resp_) {       \
                this->method_name##Impl(req_, resp_, id1, id2);                                                 \
            });                                                                                                 \
    }                                                                                                           \
    void class_name::method_name##Impl(const drogon::HttpRequestPtr& req, common::utils::ResponseWrapper& resp, \
                                       int64_t id1, int64_t id2)

// 带一个int64_t和一个std::string参数的方法宏
#define IMPLEMENT_CONTROLLER_METHOD_I64_STR(class_name, method_name, id, str)                                   \
    void class_name::method_name(const drogon::HttpRequestPtr& req,                                             \
                                 std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t id,    \
                                 const std::string&(str)) {                                                     \
        common::utils::ControllerHelper::Execute(                                                               \
            req, std::move(callback),                                                                           \
            [this, id, str](const drogon::HttpRequestPtr& req_, common::utils::ResponseWrapper& resp_) {        \
                this->method_name##Impl(req_, resp_, id, str);                                                  \
            });                                                                                                 \
    }                                                                                                           \
    void class_name::method_name##Impl(const drogon::HttpRequestPtr& req, common::utils::ResponseWrapper& resp, \
                                       int64_t id, const std::string&(str))

// 带一个std::string参数的方法宏
#define IMPLEMENT_CONTROLLER_METHOD_STR(class_name, method_name, str)                                           \
    void class_name::method_name(const drogon::HttpRequestPtr& req,                                             \
                                 std::function<void(const drogon::HttpResponsePtr&)>&& callback,                \
                                 const std::string&(str)) {                                                     \
        common::utils::ControllerHelper::Execute(                                                               \
            req, std::move(callback),                                                                           \
            [this, str](const drogon::HttpRequestPtr& req_, common::utils::ResponseWrapper& resp_) {            \
                this->method_name##Impl(req_, resp_, str);                                                      \
            });                                                                                                 \
    }                                                                                                           \
    void class_name::method_name##Impl(const drogon::HttpRequestPtr& req, common::utils::ResponseWrapper& resp, \
                                       const std::string&(str))

#endif  // QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_CONTROLLER_HELPER_H