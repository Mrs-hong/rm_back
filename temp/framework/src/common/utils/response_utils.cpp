/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/utils/response_utils.h"

namespace common {
    namespace utils {

        HttpResponse HttpResponse::Success(const std::string& message, const Json::Value& data) {
            HttpResponse response;
            response.mCode = 200;
            response.mMessage = message;
            response.mData = data;
            return response;
        }

        HttpResponse HttpResponse::Error(int code, const std::string& message, const Json::Value& data) {
            HttpResponse response;
            response.mCode = code;
            response.mMessage = message;
            response.mData = data;
            return response;
        }

        drogon::HttpResponsePtr HttpResponse::ToDrogonResponse() const {
            Json::Value ret;
            ret["code"] = mCode;
            ret["message"] = mMessage;
            if (!mData.isNull()) {
                ret["data"] = mData;
            }

            Json::StreamWriterBuilder writerBuilder;
            writerBuilder["emitUTF8"] = true;
            writerBuilder["indentation"] = "";

            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
            resp->setBody(Json::writeString(writerBuilder, ret));

            // 设置HTTP状态码
            switch (mCode) {
                case 200:
                    resp->setStatusCode(drogon::k200OK);
                    break;
                case 201:
                    resp->setStatusCode(drogon::k201Created);
                    break;
                case 400:
                    resp->setStatusCode(drogon::k400BadRequest);
                    break;
                case 401:
                    resp->setStatusCode(drogon::k401Unauthorized);
                    break;
                case 403:
                    resp->setStatusCode(drogon::k403Forbidden);
                    break;
                case 404:
                    resp->setStatusCode(drogon::k404NotFound);
                    break;
                case 500:
                    resp->setStatusCode(drogon::k500InternalServerError);
                    break;
                default:
                    resp->setStatusCode(drogon::k200OK);
                    break;
            }

            return resp;
        }

    }  // namespace utils
}  // namespace common
