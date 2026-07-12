/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "ipc/protocol.h"

#include <array>
#include <iostream>
#include <json/json.h>

namespace qifeng::scm {

    namespace {

        // 命令枚举与字符串名称的映射表（大小通过 CTAD 自动推导，无需手工维护计数常量）
        using CommandNamePair = std::pair<ScmCommand, const char*>;
        constexpr std::array kCommandNameMap = {
            CommandNamePair{ScmCommand::VERSION, "VERSION"},
            CommandNamePair{ScmCommand::INSTALL, "INSTALL"},
            CommandNamePair{ScmCommand::START, "START"},
            CommandNamePair{ScmCommand::STOP, "STOP"},
            CommandNamePair{ScmCommand::STOP_ALL, "STOP_ALL"},
            CommandNamePair{ScmCommand::RESTART, "RESTART"},
            CommandNamePair{ScmCommand::RESTART_ALL, "RESTART_ALL"},
            CommandNamePair{ScmCommand::UPGRADE, "UPGRADE"},
            CommandNamePair{ScmCommand::LIST, "LIST"},
            CommandNamePair{ScmCommand::INFO, "INFO"},
            CommandNamePair{ScmCommand::LOG, "LOG"},
            CommandNamePair{ScmCommand::UNINSTALL, "UNINSTALL"},
            CommandNamePair{ScmCommand::UNINSTALL_ALL, "UNINSTALL_ALL"},
            CommandNamePair{ScmCommand::RELOAD, "RELOAD"},
            CommandNamePair{ScmCommand::RELOAD_ALL, "RELOAD_ALL"},
            CommandNamePair{ScmCommand::KILL, "KILL"},
            CommandNamePair{ScmCommand::SLOG, "SLOG"},
            CommandNamePair{ScmCommand::CHECK, "CHECK"},
            CommandNamePair{ScmCommand::INIT_NGINX, "INIT_NGINX"},
            CommandNamePair{ScmCommand::RESET_NGINX, "RESET_NGINX"},
            CommandNamePair{ScmCommand::ADD_MODEL, "ADD_MODEL"},
            CommandNamePair{ScmCommand::CLEAR_MODEL, "CLEAR_MODEL"},
            CommandNamePair{ScmCommand::UPGRADES, "UPGRADES"}
        };

    }  // namespace

    const char* ScmCommandToString(ScmCommand cmd) {
        for (const auto& pair : kCommandNameMap) {
            if (pair.first == cmd) {
                return pair.second;
            }
        }
        return "UNKNOWN";
    }

    std::optional<ScmCommand> StringToScmCommand(const std::string& str) {
        for (const auto& pair : kCommandNameMap) {
            if (pair.second == str) {
                return pair.first;
            }
        }
        return std::nullopt;
    }

    Json::Value ScmRequest::ToJson() const {
        Json::Value root(Json::objectValue);
        root["command"] = static_cast<int>(command);
        root["params"] = params;
        return root;
    }

    std::optional<ScmRequest> ScmRequest::FromJson(const Json::Value& root) {
        if (!root.isMember("command") || !root["command"].isInt()) {
            return std::nullopt;
        }

        ScmRequest request;
        request.command = static_cast<ScmCommand>(root["command"].asInt());

        // params 字段可选，缺失时使用空对象
        if (root.isMember("params") && root["params"].isObject()) {
            request.params = root["params"];
        } else {
            request.params = Json::Value(Json::objectValue);
        }

        return request;
    }

    std::string ControlProtocol::EncodeRequest(const ScmRequest& request) {
        Json::Value json = request.ToJson();
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        std::string jsonStr = Json::writeString(builder, json);
        if (!jsonStr.empty() && jsonStr.back() == '\n') {
            jsonStr.pop_back();
        }
        return jsonStr + "\n";
    }

    std::string ControlProtocol::EncodeResponse(const ScmResponse& response) {
        Json::Value json = response.ToJsonConst();
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        std::string jsonStr = Json::writeString(builder, json);
        if (!jsonStr.empty() && jsonStr.back() == '\n') {
            jsonStr.pop_back();
        }
        return jsonStr + "\n";
    }

    bool ControlProtocol::DecodeRequest(const std::string& jsonStr, ScmRequest& request) {
        Json::CharReaderBuilder builder;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        Json::Value root;
        std::string errors;
        if (!reader->parse(jsonStr.data(), jsonStr.data() + jsonStr.size(), &root, &errors)) {
            std::cerr << "ControlProtocol::DecodeRequest parse error: " << errors << std::endl;
            return false;
        }

        auto result = ScmRequest::FromJson(root);
        if (!result.has_value()) {
            std::cerr << "ControlProtocol::DecodeRequest invalid request format" << std::endl;
            return false;
        }

        request = std::move(result.value());
        return true;
    }

    bool ControlProtocol::DecodeResponse(const std::string& jsonStr, ScmResponse& response) {
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

    std::vector<std::string> ControlProtocol::ExtractMessages(std::string& buffer) {
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
