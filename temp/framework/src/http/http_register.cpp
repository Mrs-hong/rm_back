/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <drogon/HttpRequest.h>
#include <map>
#include <string_view>

#include "drogon/HttpTypes.h"
#include "http/http_register.h"
#include "json/value.h"

namespace qifeng {

    std::string_view GetHttpCodeMessage(HttpResponseCode code) {
        static const std::map<HttpResponseCode, std::string_view> CodeToMessage = {
            {HttpResponseCode::OK, "Success"},
            {HttpResponseCode::INVALID_JSON, "Invalid JSON"},
            {HttpResponseCode::BAD_REQUEST, "Bad Request"},
            {HttpResponseCode::CREATED, "Created"},
            {HttpResponseCode::UNAUTHORIZED, "Unauthorized"},
            {HttpResponseCode::FORBIDDEN, "Forbidden"},
            {HttpResponseCode::NOT_FOUND, "Not Found"},
            {HttpResponseCode::INTERNAL_SERVER_ERROR, "Internal Server Error"},
        };

        auto iter = CodeToMessage.find(code);
        return iter != CodeToMessage.end() ? iter->second : "Unknown Error";
    }

    static Json::Value StringToJson(const std::string& jsonStr) {
        Json::Value root;
        Json::CharReaderBuilder readerBuilder;
        std::string errs;

        // 解析 JSON 字符串
        std::unique_ptr<Json::CharReader> reader(readerBuilder.newCharReader());
        if (!reader->parse(jsonStr.c_str(), jsonStr.c_str() + jsonStr.size(), &root, &errs)) {
            return Json::Value();
        }

        if (!root.isObject()) {
            return root;  // 或者根据需要返回空对象
        }
        if (root.isMember("data") && root["data"].isObject()) {
            // 返回前端不暴露后台细节需要删除 data 对象中的 "@type" 字段
            root["data"].removeMember("@type");
        }
        return root;
    }

    // ConvertHttpResp 默认resp转义函数，业务可根据需求自定义回包在HttpBinder::Bin设置
    drogon::HttpResponsePtr ConvertHttpResp(const drogon::HttpRequestPtr& httpReq, const int code,
                                            const google::protobuf::Message* respValue) {
        (void)httpReq;
        drogon::HttpResponsePtr respPtr;
        if (respValue != nullptr) {
            std::string json;
            auto status = google::protobuf::util::MessageToJsonString(*respValue, &json);
            if (!status.ok()) {
                SLOG_ERROR << "HttpResponse MessageToJsonString failed, " << status.ToString();
                respPtr = drogon::HttpResponse::newHttpResponse();
                respPtr->setStatusCode(drogon::k500InternalServerError);
                return respPtr;
            }
            Json::Value body = StringToJson(json);
            respPtr = drogon::HttpResponse::newHttpJsonResponse(body);
        }

        switch (code) {
            case HttpResponseCode::OK:
                respPtr->setStatusCode(drogon::k200OK);
                break;
            case HttpResponseCode::CREATED:
                respPtr->setStatusCode(drogon::k201Created);
                break;
            case HttpResponseCode::BAD_REQUEST:
            case HttpResponseCode::INVALID_JSON:
                respPtr->setStatusCode(drogon::k400BadRequest);
                break;
            case HttpResponseCode::UNAUTHORIZED:
                respPtr->setStatusCode(drogon::k401Unauthorized);
                break;
            case HttpResponseCode::FORBIDDEN:
                respPtr->setStatusCode(drogon::k403Forbidden);
                break;
            case HttpResponseCode::NOT_FOUND:
                respPtr->setStatusCode(drogon::k404NotFound);
                break;
            case HttpResponseCode::INTERNAL_SERVER_ERROR:
            default:
                respPtr->setStatusCode(drogon::k500InternalServerError);
                break;
        }
        return respPtr;
    }

}  // namespace qifeng
