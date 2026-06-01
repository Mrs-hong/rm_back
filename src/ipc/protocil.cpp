/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "ipc/protocol.h"

#include <json/json.h>

#include <cstring>
#include <iostream>

namespace qifeng::scm {

    const char* ScmCommandToString(ScmCommand cmd) {
        switch (cmd) {
            case ScmCommand::VERSION:
                return "VERSION";
            case ScmCommand::INSTALL:
                return "INSTALL";
            case ScmCommand::START:
                return "START";
            case ScmCommand::STOP:
                return "STOP";
            case ScmCommand::RESTART:
                return "RESTART";
            case ScmCommand::RESTART_ALL:
                return "RESTART_ALL";
            case ScmCommand::UPGRADE:
                return "UPGRADE";
            case ScmCommand::LIST:
                return "LIST";
            case ScmCommand::INFO:
                return "INFO";
            case ScmCommand::LOG:
                return "LOG";
            default:
                return "UNKNOWN";
        }
    }

    std::string ControlProtocol::EncodeRequest(const ScmRequest &request) {
        Json::Value json = request.ToJsonConst();
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        std::string jsonStr = Json::writeString(builder, json);
        if (!jsonStr.empty() && jsonStr.back() == '\n') {
            jsonStr.pop_back();
        }
        return jsonStr + "\n";
    }

    std::string ControlProtocol::EncodeResponse(const ScmResponse &response) {
        Json::Value json = response.ToJsonConst();
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        std::string jsonStr = Json::writeString(builder, json);
        if (!jsonStr.empty() && jsonStr.back() == '\n') {
            jsonStr.pop_back();
        }
        return jsonStr + "\n";
    }

    bool ControlProtocol::DecodeRequest(const std::string &jsonStr, ScmRequest &request) {
        Json::CharReaderBuilder builder;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        Json::Value root;
        std::string errors;
        if (!reader->parse(jsonStr.data(), jsonStr.data() + jsonStr.size(), &root, &errors)) {
            std::cerr << "ControlProtocol::DecodeRequest parse error: " << errors << std::endl;
            return false;
        }

        return request.fromJson(root);
    }

    bool ControlProtocol::DecodeResponse(const std::string &jsonStr, ScmResponse &response) {
        Json::CharReaderBuilder builder;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        Json::Value root;
        std::string errors;
        if (!reader->parse(jsonStr.data(), jsonStr.data() + jsonStr.size(), &root, &errors)) {
            std::cerr << "ControlProtocol::DecodeResponse parse error: " << errors << std::endl;
            return false;
        }

        return response.fromJson(root);
    }

    std::vector<std::string> ControlProtocol::ExtractMessages(std::string &buffer) {
        std::vector<std::string> messages;
        size_t pos = 0;

        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string msg = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);
            if (!msg.empty()) {
                messages.push_back(std::move(msg));
            }
        }

        return messages;
    }

}  // namespace qifeng::scm
