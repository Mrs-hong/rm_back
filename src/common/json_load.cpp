/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/json_load.h"

#include "jsoncpp/json/json.h"
#include <fstream>
#include <memory>
#include <sstream>

#include "qifeng_framework/common/logger.h"

namespace qifeng::scm {

    bool JsonLoad::LoadFromFile(const std::string &path) {
        std::ifstream ifs(path);
        if (!ifs) {
            SLOG_WARN << "[json_load] config file not found: " << path;
            mValid = false;
            return false;
        }
        std::stringstream ss;
        ss << ifs.rdbuf();
        const std::string content = ss.str();

        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        if (reader->parse(content.data(), content.data() + content.size(), &root, &errors)) {
            mRoot = std::move(root);
            mValid = true;
            SLOG_INFO << "[json_load] config loaded from " << path;
            return true;
        }
        SLOG_WARN << "[json_load] config parse failed: " << errors << " (path=" << path << ")";
        mValid = false;
        return false;
    }

    bool JsonLoad::LoadFromString(const std::string &content) {
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        if (reader->parse(content.data(), content.data() + content.size(), &root, &errors)) {
            mRoot = std::move(root);
            mValid = true;
            return true;
        }
        SLOG_WARN << "[json_load] content parse failed: " << errors;
        mValid = false;
        return false;
    }

    Json::Value JsonLoad::Get(const std::string &key) const {
        if (!mValid || !mRoot.isMember(key)) {
            return Json::Value();
        }
        return mRoot[key];
    }

}  // namespace qifeng::scm
