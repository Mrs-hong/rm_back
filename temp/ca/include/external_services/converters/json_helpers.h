//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_EXTERNAL_SERVICES_CONVERTERS_JSON_HELPERS_H
#define QIFENG_CA_EXTERNAL_SERVICES_CONVERTERS_JSON_HELPERS_H

#include <memory>
#include <string>

#include "json/reader.h"
#include "json/value.h"
#include "json/writer.h"

namespace qifeng_ca::external_services {

    inline std::string ToJsonString(const Json::Value &v) {
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        return Json::writeString(builder, v);
    }

    inline bool ParseJson(const std::string &body, Json::Value &out) {
        Json::CharReaderBuilder rb;
        std::string errs;
        std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
        return reader->parse(body.c_str(), body.c_str() + body.size(), &out, &errs);
    }

}  // namespace qifeng_ca::external_services

#endif  // QIFENG_CA_EXTERNAL_SERVICES_CONVERTERS_JSON_HELPERS_H
