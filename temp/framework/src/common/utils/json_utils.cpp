/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/utils/json_utils.h"

namespace common {
    namespace utils {
        // 显式实例化常用类型
        template void JsonUtils::ParseMember<std::string>(Json::Value&, const std::string&, std::string&, bool);
        template void JsonUtils::ParseMember<int>(Json::Value&, const std::string&, int&, bool);
        template void JsonUtils::ParseMember<double>(Json::Value&, const std::string&, double&, bool);
        template void JsonUtils::ParseMember<bool>(Json::Value&, const std::string&, bool&, bool);
        template void JsonUtils::ParseMember<Json::Int64>(Json::Value&, const std::string&, Json::Int64&, bool);
        template void JsonUtils::ParseMember<Json::UInt64>(Json::Value&, const std::string&, Json::UInt64&, bool);

        std::string JsonToCompactString(const Json::Value& val) {
            Json::StreamWriterBuilder builder;
            builder.settings_["indentation"] = "";
            return Json::writeString(builder, val);
        }
    }  // namespace utils
}  // namespace common