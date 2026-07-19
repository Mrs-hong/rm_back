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
        constexpr std::size_t CommandCount = 21;
        using CommandNamePair = std::pair<ScmCommand, const char*>;
        constexpr std::array<CommandNamePair, CommandCount> CommandNameMap = {{{ScmCommand::VERSION, "VERSION"},
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

        // =========================================================================
        // 基于 ScmRequestTraits 的模板化序列化/反序列化
        // 替代原 21 个 ParamsToJson 重载 + 21 个 ParamsFromJson 重载
        // =========================================================================

        // 字段写入 JSON：枚举类型转 int，其余类型由 Json::Value 原生支持
        template <typename MemberType>
        void WriteField(Json::Value &obj, const char* key, const MemberType &value) {
            if constexpr (std::is_enum_v<MemberType>) {
                obj[key] = static_cast<int>(value);
            } else {
                obj[key] = value;
            }
        }

        // 从 JSON 节点读取单个字段：类型匹配则赋值并返回 true，否则返回 false（不赋值）
        template <typename MemberType>
        bool ReadJsonTo(MemberType &member, const Json::Value &node) {
            if constexpr (std::is_same_v<MemberType, std::string>) {
                if (!node.isString()) {
                    return false;
                }
                member = node.asString();
                return true;
            } else if constexpr (std::is_same_v<MemberType, bool>) {
                if (!node.isBool()) {
                    return false;
                }
                member = node.asBool();
                return true;
            } else if constexpr (std::is_same_v<MemberType, int>) {
                if (!node.isInt()) {
                    return false;
                }
                member = node.asInt();
                return true;
            } else if constexpr (std::is_enum_v<MemberType>) {
                if (!node.isInt()) {
                    return false;
                }
                member = static_cast<MemberType>(node.asInt());
                return true;
            }
            return false;  // 不支持的成员类型
        }

        // 字段从 JSON 读取：必填缺失/类型不匹配返回 false，可选缺失/类型不匹配跳过返回 true
        template <typename MemberType>
        bool ReadField(MemberType &member, const Json::Value &obj, const char* key, bool required) {
            if (!obj.isMember(key)) {
                return !required;  // 必填缺失失败，可选缺失跳过
            }
            const Json::Value &node = obj[key];
            // 类型匹配则赋值返回 true；必填类型不匹配返回 false，可选类型不匹配返回 true（跳过）
            return ReadJsonTo(member, node) || !required;
        }

        // 模板版 ParamsToJson：遍历 ScmRequestTraits<T>::kFields 写入所有字段
        template <typename T>
        Json::Value ParamsToJson(const T &request) {
            Json::Value params(Json::objectValue);
            constexpr auto fields = ScmRequestTraits<T>::kFields;
            std::apply([&request,
                        &params](const auto &... descs) { ((WriteField(params, descs.key, request.*descs.ptr)), ...); },
                       fields);
            return params;
        }

        // 模板版 ParamsFromJson：遍历 kFields 读取所有字段，任一必填字段失败则整体失败
        template <typename T>
        bool ParamsFromJson(T &request, const Json::Value &params) {
            constexpr auto fields = ScmRequestTraits<T>::kFields;
            return std::apply(
                [&request, &params](const auto &... descs) -> bool {
                    return (... && ReadField(request.*descs.ptr, params, descs.key, descs.required));
                },
                fields);
        }

        // =========================================================================
        // 反序列化分派查表：替代 ScmRequest::FromJson 中的 21-case switch
        // 依赖 ScmCommand 枚举值与数组索引一一对应（variant 类型顺序与枚举顺序一致）
        // =========================================================================

        // 反序列化函数指针类型
        using DeserializerFn = bool (*)(ScmRequest &, const Json::Value &);

        // 单类型反序列化函数模板：构造对应请求类型并填充字段
        template <typename T>
        bool DeserializeRequest(ScmRequest &req, const Json::Value &params) {
            T param;
            if (!ParamsFromJson(param, params)) {
                return false;
            }
            req.data = std::move(param);
            return true;
        }

// 构造反序列化函数指针
#define SCM_DESERIALIZER(RequestType) &DeserializeRequest<RequestType>

        // 命令枚举值 -> 反序列化函数 查表（索引 = static_cast<int>(ScmCommand)）
        const std::array<DeserializerFn, CommandCount> Deserializers = {
            SCM_DESERIALIZER(VersionRequest),     // VERSION = 0
            SCM_DESERIALIZER(InstallRequest),     // INSTALL = 1
            SCM_DESERIALIZER(StartRequest),       // START = 2
            SCM_DESERIALIZER(StopRequest),        // STOP = 3
            SCM_DESERIALIZER(RestartRequest),     // RESTART = 4
            SCM_DESERIALIZER(RestartAllRequest),  // RESTART_ALL = 5
            SCM_DESERIALIZER(UpgradeRequest),     // UPGRADE = 6
            SCM_DESERIALIZER(ListRequest),        // LIST = 7
            SCM_DESERIALIZER(InfoRequest),        // INFO = 8
            SCM_DESERIALIZER(LogRequest),         // LOG = 9
            SCM_DESERIALIZER(UninstallRequest),   // UNINSTALL = 10
            SCM_DESERIALIZER(ReloadRequest),      // RELOAD = 11
            SCM_DESERIALIZER(ReloadAllRequest),   // RELOAD_ALL = 12
            SCM_DESERIALIZER(KillRequest),        // KILL = 13
            SCM_DESERIALIZER(SlogRequest),        // SLOG = 14
            SCM_DESERIALIZER(CheckRequest),       // CHECK = 15
            SCM_DESERIALIZER(InitNginxRequest),   // INIT_NGINX = 16
            SCM_DESERIALIZER(ResetNginxRequest),  // RESET_NGINX = 17
            SCM_DESERIALIZER(AddModelRequest),    // ADD_MODEL = 18
            SCM_DESERIALIZER(ClearModelRequest),  // CLEAR_MODEL = 19
            SCM_DESERIALIZER(UpgradesRequest)     // UPGRADES = 20
        };

#undef SCM_DESERIALIZER

    }  // namespace

    const char* ScmCommandToString(ScmCommand cmd) {
        for (const auto &pair : CommandNameMap) {
            if (pair.first == cmd) {
                return pair.second;
            }
        }
        return "UNKNOWN";
    }

    std::optional<ScmCommand> StringToScmCommand(const std::string &str) {
        for (const auto &pair : CommandNameMap) {
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

        // 通过查表分派到对应类型的反序列化函数（替代 21-case switch）
        // 依赖 ScmCommand 枚举值与 Deserializers 数组索引一一对应
        int idx = static_cast<int>(cmd);
        if (idx < 0 || idx >= static_cast<int>(Deserializers.size())) {
            return std::nullopt;
        }

        ScmRequest request;
        if (!Deserializers[static_cast<std::size_t>(idx)](request, params)) {
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
