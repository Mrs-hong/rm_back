/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "ipc/protocol.h"

#include "jsoncpp/json/json.h"
#include <array>
#include <iostream>

namespace qifeng::scm {

    namespace {

        // 命令枚举与字符串名称的映射表
        constexpr std::size_t kCommandCount = 21;
        using CommandNamePair = std::pair<ScmCommand, const char*>;
        constexpr std::array<CommandNamePair, kCommandCount> kCommandNameMap = {
            {{ScmCommand::VERSION, "VERSION"},
             {ScmCommand::INSTALL, "INSTALL"},
             {ScmCommand::START, "START"},
             {ScmCommand::STOP, "STOP"},
             {ScmCommand::RESTART, "RESTART"},
             {ScmCommand::RESTART_ALL, "RESTART_ALL"},
             {ScmCommand::UPGRADE, "UPGRADE"},
             {ScmCommand::LIST, "LIST"},
             {ScmCommand::INFO, "INFO"},
             {ScmCommand::LOG, "LOG"},
             {ScmCommand::UNINSTALL, "UNINSTALL"},
             {ScmCommand::RELOAD, "RELOAD"},
             {ScmCommand::RELOAD_ALL, "RELOAD_ALL"},
             {ScmCommand::KILL, "KILL"},
             {ScmCommand::SLOG, "SLOG"},
             {ScmCommand::CHECK, "CHECK"},
             {ScmCommand::INIT_NGINX, "INIT_NGINX"},
             {ScmCommand::RESET_NGINX, "RESET_NGINX"},
             {ScmCommand::ADD_MODEL, "ADD_MODEL"},
             {ScmCommand::CLEAR_MODEL, "CLEAR_MODEL"},
             {ScmCommand::UPGRADES, "UPGRADES"}}};

        // 各命令参数结构体的 JSON 序列化辅助函数
        Json::Value ParamsToJson(const VersionRequest & /*request*/) {
            return Json::Value(Json::objectValue);
        }

        Json::Value ParamsToJson(const InstallRequest &request) {
            Json::Value params(Json::objectValue);
            params["serviceName"] = request.serviceName;
            params["tarDir"] = request.tarDir;
            return params;
        }

        Json::Value ParamsToJson(const StartRequest &request) {
            Json::Value params(Json::objectValue);
            params["serviceName"] = request.serviceName;
            return params;
        }

        Json::Value ParamsToJson(const StopRequest &request) {
            Json::Value params(Json::objectValue);
            params["serviceName"] = request.serviceName;
            return params;
        }

        Json::Value ParamsToJson(const RestartRequest &request) {
            Json::Value params(Json::objectValue);
            params["serviceName"] = request.serviceName;
            return params;
        }

        Json::Value ParamsToJson(const RestartAllRequest & /*request*/) {
            return Json::Value(Json::objectValue);
        }

        Json::Value ParamsToJson(const UpgradeRequest &request) {
            Json::Value params(Json::objectValue);
            params["serviceName"] = request.serviceName;
            params["tarDir"] = request.tarDir;
            return params;
        }

        Json::Value ParamsToJson(const ListRequest & /*request*/) {
            return Json::Value(Json::objectValue);
        }

        Json::Value ParamsToJson(const InfoRequest &request) {
            Json::Value params(Json::objectValue);
            params["serviceName"] = request.serviceName;
            params["infoDetail"] = request.infoDetail;
            return params;
        }

        Json::Value ParamsToJson(const LogRequest &request) {
            Json::Value params(Json::objectValue);
            params["logLevel"] = request.logLevel;
            params["logCount"] = request.logCount;
            return params;
        }

        Json::Value ParamsToJson(const UninstallRequest &request) {
            Json::Value params(Json::objectValue);
            params["serviceName"] = request.serviceName;
            return params;
        }

        Json::Value ParamsToJson(const ReloadRequest &request) {
            Json::Value params(Json::objectValue);
            params["serviceName"] = request.serviceName;
            return params;
        }

        Json::Value ParamsToJson(const ReloadAllRequest & /*request*/) {
            return Json::Value(Json::objectValue);
        }

        Json::Value ParamsToJson(const KillRequest & /*request*/) {
            return Json::Value(Json::objectValue);
        }

        Json::Value ParamsToJson(const SlogRequest &request) {
            Json::Value params(Json::objectValue);
            params["serviceName"] = request.serviceName;
            params["logCount"] = request.logCount;
            return params;
        }

        Json::Value ParamsToJson(const CheckRequest &request) {
            Json::Value params(Json::objectValue);
            params["configPath"] = request.configPath;
            return params;
        }

        Json::Value ParamsToJson(const InitNginxRequest &request) {
            Json::Value params(Json::objectValue);
            params["dirPath"] = request.dirPath;
            return params;
        }

        Json::Value ParamsToJson(const ResetNginxRequest &request) {
            Json::Value params(Json::objectValue);
            params["mode"] = static_cast<int>(request.mode);
            return params;
        }

        Json::Value ParamsToJson(const AddModelRequest &request) {
            Json::Value params(Json::objectValue);
            params["srcPath"] = request.srcPath;
            return params;
        }

        Json::Value ParamsToJson(const ClearModelRequest &request) {
            Json::Value params(Json::objectValue);
            params["modelName"] = request.modelName;
            return params;
        }

        Json::Value ParamsToJson(const UpgradesRequest &request) {
            Json::Value params(Json::objectValue);
            params["serviceName"] = request.serviceName;
            params["tarDir"] = request.tarDir;
            return params;
        }

        // 从 JSON 反序列化各命令参数
        bool ParamsFromJson(VersionRequest & /*request*/, const Json::Value & /*params*/) {
            return true;
        }

        bool ParamsFromJson(InstallRequest &request, const Json::Value &params) {
            if (!params.isMember("serviceName") || !params["serviceName"].isString()) {
                return false;
            }
            request.serviceName = params["serviceName"].asString();
            if (params.isMember("tarDir") && params["tarDir"].isString()) {
                request.tarDir = params["tarDir"].asString();
            }
            return true;
        }

        bool ParamsFromJson(StartRequest &request, const Json::Value &params) {
            if (!params.isMember("serviceName") || !params["serviceName"].isString()) {
                return false;
            }
            request.serviceName = params["serviceName"].asString();
            return true;
        }

        bool ParamsFromJson(StopRequest &request, const Json::Value &params) {
            if (!params.isMember("serviceName") || !params["serviceName"].isString()) {
                return false;
            }
            request.serviceName = params["serviceName"].asString();
            return true;
        }

        bool ParamsFromJson(RestartRequest &request, const Json::Value &params) {
            if (!params.isMember("serviceName") || !params["serviceName"].isString()) {
                return false;
            }
            request.serviceName = params["serviceName"].asString();
            return true;
        }

        bool ParamsFromJson(RestartAllRequest & /*request*/, const Json::Value & /*params*/) {
            return true;
        }

        bool ParamsFromJson(UpgradeRequest &request, const Json::Value &params) {
            if (!params.isMember("serviceName") || !params["serviceName"].isString()) {
                return false;
            }
            request.serviceName = params["serviceName"].asString();
            if (params.isMember("tarDir") && params["tarDir"].isString()) {
                request.tarDir = params["tarDir"].asString();
            }
            return true;
        }

        bool ParamsFromJson(ListRequest & /*request*/, const Json::Value & /*params*/) {
            return true;
        }

        bool ParamsFromJson(InfoRequest &request, const Json::Value &params) {
            if (!params.isMember("serviceName") || !params["serviceName"].isString()) {
                return false;
            }
            request.serviceName = params["serviceName"].asString();
            if (params.isMember("infoDetail") && params["infoDetail"].isBool()) {
                request.infoDetail = params["infoDetail"].asBool();
            }
            return true;
        }

        bool ParamsFromJson(LogRequest &request, const Json::Value &params) {
            if (params.isMember("logLevel") && params["logLevel"].isInt()) {
                request.logLevel = params["logLevel"].asInt();
            }
            if (params.isMember("logCount") && params["logCount"].isInt()) {
                request.logCount = params["logCount"].asInt();
            }
            return true;
        }

        bool ParamsFromJson(UninstallRequest &request, const Json::Value &params) {
            if (!params.isMember("serviceName") || !params["serviceName"].isString()) {
                return false;
            }
            request.serviceName = params["serviceName"].asString();
            return true;
        }

        bool ParamsFromJson(ReloadRequest &request, const Json::Value &params) {
            if (!params.isMember("serviceName") || !params["serviceName"].isString()) {
                return false;
            }
            request.serviceName = params["serviceName"].asString();
            return true;
        }

        bool ParamsFromJson(ReloadAllRequest & /*request*/, const Json::Value & /*params*/) {
            return true;
        }

        bool ParamsFromJson(KillRequest & /*request*/, const Json::Value & /*params*/) {
            return true;
        }

        bool ParamsFromJson(SlogRequest &request, const Json::Value &params) {
            if (!params.isMember("serviceName") || !params["serviceName"].isString()) {
                return false;
            }
            request.serviceName = params["serviceName"].asString();
            if (params.isMember("logCount") && params["logCount"].isInt()) {
                request.logCount = params["logCount"].asInt();
            }
            return true;
        }

        bool ParamsFromJson(CheckRequest &request, const Json::Value &params) {
            if (params.isMember("configPath") && params["configPath"].isString()) {
                request.configPath = params["configPath"].asString();
            }
            return true;
        }

        bool ParamsFromJson(InitNginxRequest &request, const Json::Value &params) {
            if (params.isMember("dirPath") && params["dirPath"].isString()) {
                request.dirPath = params["dirPath"].asString();
            }
            return true;
        }

        bool ParamsFromJson(ResetNginxRequest &request, const Json::Value &params) {
            if (params.isMember("mode") && params["mode"].isInt()) {
                request.mode = static_cast<NginxResetMode>(params["mode"].asInt());
            }
            return true;
        }

        bool ParamsFromJson(AddModelRequest &request, const Json::Value &params) {
            if (!params.isMember("srcPath") || !params["srcPath"].isString()) {
                return false;
            }
            request.srcPath = params["srcPath"].asString();
            return true;
        }

        bool ParamsFromJson(ClearModelRequest &request, const Json::Value &params) {
            if (!params.isMember("modelName") || !params["modelName"].isString()) {
                return false;
            }
            request.modelName = params["modelName"].asString();
            return true;
        }

        bool ParamsFromJson(UpgradesRequest &request, const Json::Value &params) {
            if (!params.isMember("serviceName") || !params["serviceName"].isString()) {
                return false;
            }
            request.serviceName = params["serviceName"].asString();
            // tarDir 可选字段，缺失或非字符串时默认为空字符串
            if (params.isMember("tarDir") && params["tarDir"].isString()) {
                request.tarDir = params["tarDir"].asString();
            }
            return true;
        }

    }  // namespace

    const char* ScmCommandToString(ScmCommand cmd) {
        for (const auto &pair : kCommandNameMap) {
            if (pair.first == cmd) {
                return pair.second;
            }
        }
        return "UNKNOWN";
    }

    std::optional<ScmCommand> StringToScmCommand(const std::string &str) {
        for (const auto &pair : kCommandNameMap) {
            if (pair.second == str) {
                return pair.first;
            }
        }
        return std::nullopt;
    }

    Json::Value ScmRequest::ToJson() const {
        Json::Value root(Json::objectValue);
        root["command"] = static_cast<int>(Command());
        root["params"] = std::visit([](const auto &param) -> Json::Value { return ParamsToJson(param); }, data);
        return root;
    }

    std::optional<ScmRequest> ScmRequest::FromJson(const Json::Value &root) {
        if (!root.isMember("command") || !root["command"].isInt()) {
            return std::nullopt;
        }

        int cmdValue = root["command"].asInt();
        auto cmd = static_cast<ScmCommand>(cmdValue);

        const Json::Value &params = root.isMember("params") ? root["params"] : Json::Value::nullSingleton();
        if (!params.isObject() && !params.isNull()) {
            return std::nullopt;
        }

        ScmRequest request;
        switch (cmd) {
            case ScmCommand::VERSION: {
                VersionRequest param;
                if (!ParamsFromJson(param, params))
                    return std::nullopt;
                request.data = param;
                break;
            }
            case ScmCommand::INSTALL: {
                InstallRequest param;
                if (!ParamsFromJson(param, params))
                    return std::nullopt;
                request.data = param;
                break;
            }
            case ScmCommand::START: {
                StartRequest param;
                if (!ParamsFromJson(param, params))
                    return std::nullopt;
                request.data = param;
                break;
            }
            case ScmCommand::STOP: {
                StopRequest param;
                if (!ParamsFromJson(param, params))
                    return std::nullopt;
                request.data = param;
                break;
            }
            case ScmCommand::RESTART: {
                RestartRequest param;
                if (!ParamsFromJson(param, params))
                    return std::nullopt;
                request.data = param;
                break;
            }
            case ScmCommand::RESTART_ALL: {
                RestartAllRequest param;
                if (!ParamsFromJson(param, params))
                    return std::nullopt;
                request.data = param;
                break;
            }
            case ScmCommand::UPGRADE: {
                UpgradeRequest param;
                if (!ParamsFromJson(param, params))
                    return std::nullopt;
                request.data = param;
                break;
            }
            case ScmCommand::LIST: {
                ListRequest param;
                if (!ParamsFromJson(param, params))
                    return std::nullopt;
                request.data = param;
                break;
            }
            case ScmCommand::INFO: {
                InfoRequest param;
                if (!ParamsFromJson(param, params))
                    return std::nullopt;
                request.data = param;
                break;
            }
            case ScmCommand::LOG: {
                LogRequest param;
                if (!ParamsFromJson(param, params))
                    return std::nullopt;
                request.data = param;
                break;
            }
            case ScmCommand::UNINSTALL: {
                UninstallRequest param;
                if (!ParamsFromJson(param, params))
                    return std::nullopt;
                request.data = param;
                break;
            }
            case ScmCommand::RELOAD: {
                ReloadRequest param;
                if (!ParamsFromJson(param, params))
                    return std::nullopt;
                request.data = param;
                break;
            }
            case ScmCommand::RELOAD_ALL: {
                ReloadAllRequest param;
                if (!ParamsFromJson(param, params))
                    return std::nullopt;
                request.data = param;
                break;
            }
            case ScmCommand::KILL: {
                KillRequest param;
                if (!ParamsFromJson(param, params))
                    return std::nullopt;
                request.data = param;
                break;
            }
            case ScmCommand::SLOG: {
                SlogRequest param;
                if (!ParamsFromJson(param, params))
                    return std::nullopt;
                request.data = param;
                break;
            }
            case ScmCommand::CHECK: {
                CheckRequest param;
                if (!ParamsFromJson(param, params))
                    return std::nullopt;
                request.data = param;
                break;
            }
            case ScmCommand::INIT_NGINX: {
                InitNginxRequest param;
                if (!ParamsFromJson(param, params))
                    return std::nullopt;
                request.data = param;
                break;
            }
            case ScmCommand::RESET_NGINX: {
                ResetNginxRequest param;
                if (!ParamsFromJson(param, params))
                    return std::nullopt;
                request.data = param;
                break;
            }
            case ScmCommand::ADD_MODEL: {
                AddModelRequest param;
                if (!ParamsFromJson(param, params))
                    return std::nullopt;
                request.data = param;
                break;
            }
            case ScmCommand::CLEAR_MODEL: {
                ClearModelRequest param;
                if (!ParamsFromJson(param, params))
                    return std::nullopt;
                request.data = param;
                break;
            }
            case ScmCommand::UPGRADES: {
                UpgradesRequest param;
                if (!ParamsFromJson(param, params))
                    return std::nullopt;
                request.data = param;
                break;
            }
            default:
                return std::nullopt;
        }

        return request;
    }

    std::string ControlProtocol::EncodeRequest(const ScmRequest &request) {
        Json::Value json = request.ToJson();
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

        auto result = ScmRequest::FromJson(root);
        if (!result.has_value()) {
            std::cerr << "ControlProtocol::DecodeRequest invalid request format" << std::endl;
            return false;
        }

        request = std::move(result.value());
        return true;
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
