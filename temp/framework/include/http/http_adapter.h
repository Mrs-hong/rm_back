/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_HTTP_HTTP_ADAPTER_H
#define QIFENG_FRAMEWORK_INCLUDE_HTTP_HTTP_ADAPTER_H

#include <functional>
#include <string_view>
#include <utility>

#include "common/logger.h"
#include "drogon/DrClassMap.h"
#include "drogon/HttpRequest.h"
#include "drogon/HttpResponse.h"
#include "google/protobuf/util/json_util.h"
#include "workflow/WFTaskFactory.h"

// 封装Drogon的http功能。理论上是由"IO(Drogon) + Handler(Workflow)"的处理方式，将IO与业务处理分割开
namespace qifeng {

    // 内置错误码(-1到-100为架构错误码预留)
    enum HttpResponseCode : int {
        INTERNAL_SERVER_ERROR = -7,
        NOT_FOUND = -6,
        FORBIDDEN = -5,
        UNAUTHORIZED = -4,
        CREATED = -3,
        BAD_REQUEST = -2,
        INVALID_JSON = -1,
        OK = 0,  // 默认正常
    };

    // URL的额外参数
    struct ContextEx {
        int64_t mId;
        uint64_t mUId;
        std::string mStr;
    };

    class ControllerExecutor {
    public:
        // HandlerRet 为业务处理函数的返回值类型（默认 int 兼容现有 int(req, resp) 签名）
        template <typename Req, typename Resp, typename PreReqFunc, typename AfterConvertRespFunc,
                  typename HandlerRet = int>
        struct ProcessController {
            using Handler = std::function<HandlerRet(const Req&, Resp&)>;

            PreReqFunc mPreReqFunc;
            Handler mHandler;
            AfterConvertRespFunc mAfterConvertRespFunc;

            ProcessController(PreReqFunc preReqFunc, Handler handler, AfterConvertRespFunc afterConvertRespFunc)
                : mPreReqFunc(preReqFunc), mHandler(handler), mAfterConvertRespFunc(afterConvertRespFunc) {
            }
        };

        template <typename Req, typename Resp, typename PreReqFunc, typename AfterConvertRespFunc,
                  typename HandlerRet = int>
        static void Execute(const drogon::HttpRequestPtr& httpReq,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                            ProcessController<Req, Resp, PreReqFunc, AfterConvertRespFunc, HandlerRet>&& controller) {
            Resp resp;
            try {
                Req req;
                std::string_view body = httpReq->getBody();
                auto contentType = httpReq->getHeader("Content-Type");
                if (!body.empty() && contentType.find("multipart/form-data") == std::string::npos) {
                    google::protobuf::util::JsonParseOptions options;
                    options.ignore_unknown_fields = true;
                    auto status = google::protobuf::util::JsonStringToMessage(body, &req);
                    if (!status.ok()) {
                        SLOG_ERROR << "Http Req JsonStringToMessage failed, " << status.ToString();
                        return callback(controller.mAfterConvertRespFunc(
                            httpReq, HandlerRet {static_cast<int>(HttpResponseCode::BAD_REQUEST)}, nullptr));
                    }
                }

                // req前置处理(如：业务通过Drogon获取上下文存储的数据)
                if (controller.mPreReqFunc != nullptr) {
                    HandlerRet preRet = controller.mPreReqFunc(httpReq, req);
                    if (!preRet) {
                        return callback(controller.mAfterConvertRespFunc(httpReq, preRet, nullptr));
                    }
                }

                // 正式处理业务逻辑
                HandlerRet handlerRet = controller.mHandler(req, resp);

                callback(controller.mAfterConvertRespFunc(httpReq, handlerRet, &resp));
            } catch (const std::exception& e) {
                SLOG_ERROR << "Http Handler Run error: " << e.what() << " [type: " << typeid(e).name() << "]";
                // 系统错误不对外抛出细节，统一返回 500
                callback(controller.mAfterConvertRespFunc(
                    httpReq, HandlerRet {static_cast<int>(HttpResponseCode::INTERNAL_SERVER_ERROR)}, nullptr));
            } catch (...) {
                SLOG_ERROR << "Http Handler caught unknown exception (typeid unavailable)";
                callback(controller.mAfterConvertRespFunc(
                    httpReq, HandlerRet {static_cast<int>(HttpResponseCode::INTERNAL_SERVER_ERROR)}, nullptr));
            }
        }
    };

    // HandlerRet 默认 int 兼容现有 int(req, resp) 签名
    // 业务可使用 Status 等自定义返回值，配合 QIFENG_METHOD_AFTERRESP_ADD 传入对应的 AfterConvertRespFunc
    // 注意：
    //  1. HandlerRet 必须支持传入int类型的构造函数
    //  2. handlerRet 必须重构bool功能，用做返回值错误校验
    template <typename Controller, typename Req, typename Resp, typename HandlerRet>
    class HttpBinder {
    public:
        using Method = HandlerRet (Controller::*)(const Req&, Resp&);
        using PreReqCallBack = std::function<HandlerRet(const drogon::HttpRequestPtr&, Req&)>;
        // AfterConvertRespCallBack 接收 HandlerRet（而非 HttpResponseCode），避免返回值信息丢失
        using AfterConvertRespCallBack = std::function<drogon::HttpResponsePtr(
            const drogon::HttpRequestPtr&, const HandlerRet&, const google::protobuf::Message*)>;

        static std::function<void(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&&)>
        Bind(Method method, PreReqCallBack preReqFunc, AfterConvertRespCallBack afterConvertRespFunc) {
            return BindImpl(method, std::move(preReqFunc), std::move(afterConvertRespFunc));
        }

    private:
        template <typename PreReqFunc, typename AfterConvertRespFunc>
        static auto BindImpl(Method method, PreReqFunc&& preReqFunc, AfterConvertRespFunc&& afterConvertRespFunc) {
            using ControllerType =
                ControllerExecutor::ProcessController<Req, Resp, PreReqFunc, AfterConvertRespFunc, HandlerRet>;

            auto self = drogon::DrClassMap::getSingleInstance<Controller>();
            if (self == nullptr) {
                drogon::DrClassMap::registerClass(Controller::classTypeName(), nullptr,
                                                  []() -> std::shared_ptr<drogon::DrObjectBase> {
                                                      static auto Inst = std::make_shared<Controller>();
                                                      return Inst;
                                                  });
                self = drogon::DrClassMap::getSingleInstance<Controller>();
            }
            return [self, method, preReqFunc = std::forward<PreReqFunc>(preReqFunc),
                    afterConvertRespFunc = std::forward<AfterConvertRespFunc>(afterConvertRespFunc)](
                       const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& cb) mutable {
                auto httpTask = WFTaskFactory::create_go_task(
                    "http_task", [self, method, preReqFunc, afterConvertRespFunc, req, cb = std::move(cb)]() mutable {
                        auto func = [self, method](const Req& r, Resp& s) -> HandlerRet {
                            return (self.get()->*method)(r, s);
                        };

                        ControllerType controller(std::move(preReqFunc), std::move(func),
                                                  std::move(afterConvertRespFunc));

                        ControllerExecutor::Execute<Req, Resp, PreReqFunc, AfterConvertRespFunc, HandlerRet>(
                            req, std::move(cb), std::move(controller));
                    });
                httpTask->start();
            };
        }
    };

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_INCLUDE_HTTP_HTTP_ADAPTER_H
