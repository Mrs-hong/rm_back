//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_PROTO_UTILS_H
#define QIFENG_CA_INCLUDE_COMMON_PROTO_UTILS_H

#include <string>

#include <google/protobuf/util/json_util.h>

#include "qifeng_framework/common/logger.h"

namespace qifeng_ca {
    // TODO(cofe): 后续放在framework下
    namespace proto_utils {

        inline bool JsonToMessage(const std::string &json, google::protobuf::Message &message) {
            google::protobuf::util::JsonParseOptions options;
            options.ignore_unknown_fields = true;
            auto status = google::protobuf::util::JsonStringToMessage(json, &message);
            if (!status.ok()) {
                SLOG_ERROR << "JsonToMessage failed: " << status.ToString();
                return false;
            }
            return true;
        }

        inline std::string MessageToJson(const google::protobuf::Message &message) {
            std::string json;
            auto status = google::protobuf::util::MessageToJsonString(message, &json);
            if (!status.ok()) {
                SLOG_ERROR << "MessageToJson failed: " << status.ToString();
                return "";
            }
            return json;
        }

    }  // namespace proto_utils
}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_PROTO_UTILS_H
