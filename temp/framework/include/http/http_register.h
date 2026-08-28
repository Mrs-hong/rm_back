/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_HTTP_HTTP_REGISTER_H
#define QIFENG_FRAMEWORK_INCLUDE_HTTP_HTTP_REGISTER_H

#include <drogon/HttpRequest.h>
#include <string>

#include "drogon/HttpAppFramework.h"
#include "drogon/utils/HttpConstraint.h"

#include "http/http_adapter.h"

namespace qifeng {

    class HttpRegistry {
    public:
        using Func = std::function<void()>;

        static HttpRegistry& Instance() {
            static HttpRegistry Instance;
            return Instance;
        }

        void Add(Func f) {
            mFuncs.emplace_back(std::move(f));
        }

        void RegisterAll() {
            for (auto& f : mFuncs) {
                f();
            }
        }

    private:
        HttpRegistry() = default;

        std::vector<Func> mFuncs;
    };

    // ConvertHttpResp 默认resp转义函数，业务可根据需求自定义回包在HttpBinder::Bin设置
    drogon::HttpResponsePtr ConvertHttpResp(const drogon::HttpRequestPtr& httpReq, const int code,
                                            const google::protobuf::Message* respValue);

    std::string_view GetHttpCodeMessage(HttpResponseCode code);

    template <typename T>
    struct MethodTraits;

    // 函数签名萃取，支持任意返回值类型（int / Status 等）
    template <typename C, typename Ret, typename Req, typename Resp>
    struct MethodTraits<Ret (C::*)(const Req&, Resp&)> {
        using RetType = Ret;
        using ReqType = Req;
        using RespType = Resp;
    };

    template <typename Controller, typename Method>
    struct MethodOptions {
        using Traits = MethodTraits<Method>;
        using RetType = typename Traits::RetType;
        using Req = typename Traits::ReqType;
        using Resp = typename Traits::RespType;

        // HandlerRet 与 Method 返回值类型一致，AfterConvertRespFunc 直接接收业务返回值
        using PreReq = typename qifeng::HttpBinder<Controller, Req, Resp, RetType>::PreReqCallBack;
        using AfterConvertResp = typename qifeng::HttpBinder<Controller, Req, Resp, RetType>::AfterConvertRespCallBack;

        PreReq mPreReqFunc = nullptr;
        AfterConvertResp mAfterConvertResp = ConvertHttpResp;
        std::string mName = "";
        std::vector<drogon::internal::HttpConstraint> mConstraints;
    };

    template <typename Controller, typename Method>
    void RegisterMethodAuto(const std::string& pattern, Method method, MethodOptions<Controller, Method> opts) {
        using Traits = MethodTraits<Method>;
        using RetType = typename Traits::RetType;
        using Req = typename Traits::ReqType;
        using Resp = typename Traits::RespType;

        using Binder = qifeng::HttpBinder<Controller, Req, Resp, RetType>;

        auto handler = Binder::Bind(method, opts.mPreReqFunc, opts.mAfterConvertResp);

        std::string path = pattern;
        if (path.empty() || path[0] != '/') {
            path = "/" + path;
        }

        drogon::app().registerHandler(path, std::move(handler), opts.mConstraints, opts.mName);
    }
}  // namespace qifeng

// Controller是类名，使用方式如Drogon的 "METHOD_ADD" 一致
#define QIFENG_METHOD_LIST_BEGIN(Controller) \
    static void QifengInitPathRouting() {    \
        using Self = Controller

// 不带PreReqFunc、ConvertHttpResp 参数（HandlerRet = int 兼容默认 int(req, resp) 签名）
#define QIFENG_METHOD_ADD(method, path, ...)                                                \
    qifeng::RegisterMethodAuto<Self>(path, &Self::method,                                   \
                                     qifeng::MethodOptions<Self, decltype(&Self::method)> { \
                                         nullptr, qifeng::ConvertHttpResp, #method, {__VA_ARGS__}})

// 带PreReqFunc, 不带ConvertHttpResp 参数
// 该宏定义用来注册一个Req前置处理方法，业务可灵活处理Req时机以及提前从 drogon::HttpRequestPtr 中获取数据
#define QIFENG_METHOD_PREREQ_ADD(method, preReqFunc, path, ...)                             \
    qifeng::RegisterMethodAuto<Self>(path, &Self::method,                                   \
                                     qifeng::MethodOptions<Self, decltype(&Self::method)> { \
                                         preReqFunc, qifeng::ConvertHttpResp, #method, {__VA_ARGS__}})

// 不带PreReqFunc, 带ConvertHttpResp 参数
// AfterConvertRespFunc 接收 HandlerRet（与 method 返回值类型一致），业务可自定义回包结构
#define QIFENG_METHOD_AFTERRESP_ADD(method, AfterConvertRespFunc, path, ...) \
    qifeng::RegisterMethodAuto<Self>(                                        \
        path, &Self::method,                                                 \
        qifeng::MethodOptions<Self, decltype(&Self::method)> {nullptr, AfterConvertRespFunc, #method, {__VA_ARGS__}})

// 带PreReqFunc、ConvertHttpResp 参数
#define QIFENG_METHOD_PREREQ_AFTERRESP_ADD(method, preReqFunc, AfterConvertRespFunc, path, ...) \
    qifeng::RegisterMethodAuto<Self>(path, &Self::method,                                       \
                                     qifeng::MethodOptions<Self, decltype(&Self::method)> {     \
                                         preReqFunc, AfterConvertRespFunc, #method, {__VA_ARGS__}})

#define QIFENG_METHOD_LIST_END                                           \
    }                                                                    \
    struct QifengAutoHttpMethodRegister {                                \
        QifengAutoHttpMethodRegister() {                                 \
            qifeng::HttpRegistry::Instance().Add(QifengInitPathRouting); \
        }                                                                \
    }

// 使用Drogon就一定要调用，建议在cpp文件中调用
#define QIFENG_HTTP_METHOD_REGISTRY(Controller) \
    static Controller::QifengAutoHttpMethodRegister QifengAutoHttpMethodRegisterFinal

// 使用Drogon Filter就一定要调用，建议在cpp文件中调用
#define QIFENG_HTTP_FILTER_REGISTRY(FilterClass)                                       \
    template <typename Filter>                                                         \
    struct QifengAutoHttpFilterRegister {                                              \
        QifengAutoHttpFilterRegister() {                                               \
            std::shared_ptr<Filter> QifengFilterPtr = std::make_shared<FilterClass>(); \
            drogon::app().registerFilter<Filter>(QifengFilterPtr);                     \
        }                                                                              \
    };                                                                                 \
    static QifengAutoHttpFilterRegister<FilterClass> QifengAutoHttpFilterRegisterFinal

#endif  // QIFENG_FRAMEWORK_INCLUDE_HTTP_HTTP_REGISTER_H
