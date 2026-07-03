/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <json/json.h>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>

#include "common/utils/json_utils.h"

namespace qifeng::scm {

    /**
     * @brief scmctl命令类型枚举
     * 定义scmctl支持的所有命令类型，用于scmctl和scmd之间的通信
     */
    enum class ScmCommand {
        VERSION = 0,  // --version / -v 查看版本
        INSTALL,      // install --name [--tar_dir] 安装服务
        START,        // start --name [--version] 启动服务
        STOP,         // stop --name 停止服务
        RESTART,      // restart --name 重启服务
        RESTART_ALL,  // restart -a 重启全部
        UPGRADE,      // upgrade --name --tar_dir 升级服务
        LIST,         // list 查看所有服务
        INFO,         // info --name 查看服务详情
        LOG,          // log --type -n 查看操作日志
        UNINSTALL,    // uninstall --name 卸载服务
        RELOAD,       // reload --name 重载服务配置
        RELOAD_ALL,   // reload -a 重载全部服务配置
        KILL,         // kill 使scmd优雅退出
        SLOG,         // slog --name [--count] 查看服务 journal 日志
        CHECK         // check 设备自检
    };

    /**
     * @brief 命令类型转字符串（用于日志和调试）
     */
    const char* ScmCommandToString(ScmCommand cmd);

    /**
     * @brief 字符串转命令类型
     * @param str 命令名字符串
     * @return 成功返回对应枚举值，失败返回 std::nullopt
     */
    std::optional<ScmCommand> StringToScmCommand(const std::string& str);

    // =========================================================================
    // 各命令专用请求参数结构体
    // =========================================================================

    struct VersionRequest {};

    struct InstallRequest {
        std::string serviceName;  // 服务名称
        std::string tarDir;       // tar包目录
    };

    struct StartRequest {
        std::string serviceName;  // 服务名称
    };

    struct StopRequest {
        std::string serviceName;  // 服务名称
    };

    struct RestartRequest {
        std::string serviceName;  // 服务名称
    };

    struct RestartAllRequest {};

    struct UpgradeRequest {
        std::string serviceName;  // 服务名称
        std::string tarDir;       // 新版本tar包目录
    };

    struct ListRequest {};

    struct InfoRequest {
        std::string serviceName;   // 服务名称
        bool infoDetail {false};   // 是否显示错误详情
    };

    struct LogRequest {
        int logLevel {0};   // 日志级别过滤
        int logCount {0};   // 日志条数
    };

    struct UninstallRequest {
        std::string serviceName;  // 服务名称
    };

    struct ReloadRequest {
        std::string serviceName;  // 服务名称
    };

    struct ReloadAllRequest {};

    struct KillRequest {};

    struct SlogRequest {
        std::string serviceName;  // 服务名称
        int logCount {0};         // 日志行数
    };

    struct CheckRequest {
        std::string configPath;  // 自检配置文件路径（可选，为空则使用默认路径）
    };

    /**
     * @brief 命令请求参数联合体
     * 每个命令对应一种参数类型，避免所有命令字段混用
     */
    using ScmRequestData = std::variant<
        VersionRequest,
        InstallRequest,
        StartRequest,
        StopRequest,
        RestartRequest,
        RestartAllRequest,
        UpgradeRequest,
        ListRequest,
        InfoRequest,
        LogRequest,
        UninstallRequest,
        ReloadRequest,
        ReloadAllRequest,
        KillRequest,
        SlogRequest,
        CheckRequest
    >;

    /**
     * @brief scmctl请求结构体
     * 定义scmctl发送给scmd的请求数据
     */
    struct ScmRequest {
        ScmRequestData data;

        /**
         * @brief 根据当前参数推导命令类型
         * @return 对应的 ScmCommand 枚举值
         */
        ScmCommand Command() const {
            return std::visit([](const auto& param) -> ScmCommand {
                using T = std::decay_t<decltype(param)>;
                if constexpr (std::is_same_v<T, VersionRequest>) return ScmCommand::VERSION;
                if constexpr (std::is_same_v<T, InstallRequest>) return ScmCommand::INSTALL;
                if constexpr (std::is_same_v<T, StartRequest>) return ScmCommand::START;
                if constexpr (std::is_same_v<T, StopRequest>) return ScmCommand::STOP;
                if constexpr (std::is_same_v<T, RestartRequest>) return ScmCommand::RESTART;
                if constexpr (std::is_same_v<T, RestartAllRequest>) return ScmCommand::RESTART_ALL;
                if constexpr (std::is_same_v<T, UpgradeRequest>) return ScmCommand::UPGRADE;
                if constexpr (std::is_same_v<T, ListRequest>) return ScmCommand::LIST;
                if constexpr (std::is_same_v<T, InfoRequest>) return ScmCommand::INFO;
                if constexpr (std::is_same_v<T, LogRequest>) return ScmCommand::LOG;
                if constexpr (std::is_same_v<T, UninstallRequest>) return ScmCommand::UNINSTALL;
                if constexpr (std::is_same_v<T, ReloadRequest>) return ScmCommand::RELOAD;
                if constexpr (std::is_same_v<T, ReloadAllRequest>) return ScmCommand::RELOAD_ALL;
                if constexpr (std::is_same_v<T, KillRequest>) return ScmCommand::KILL;
                if constexpr (std::is_same_v<T, SlogRequest>) return ScmCommand::SLOG;
                if constexpr (std::is_same_v<T, CheckRequest>) return ScmCommand::CHECK;
                return ScmCommand::VERSION;  // 不会到达
            }, data);
        }

        /**
         * @brief 将请求序列化为 Json::Value
         * @return JSON 对象
         */
        Json::Value ToJson() const;

        /**
         * @brief 从 Json::Value 反序列化为请求
         * @param root JSON 对象
         * @return 成功返回 ScmRequest，失败返回 std::nullopt
         */
        static std::optional<ScmRequest> FromJson(const Json::Value& root);
    };

    /**
     * @brief scmd响应结构体
     * 定义scmd返回给scmctl的响应数据
     * 使用json_utils.h宏实现JSON序列化/反序列化
     */
    struct ScmResponse {
        int code {0};         // 0:成功, -1:失败, 1:警告
        std::string message;  // 结果消息
        Json::Value data;     // 灵活的数据载荷（服务列表、运行时信息等）

        BEGIN_JSON_PARSER
        ADD_MEMBER(code)
        ADD_MEMBER(message)
        ADD_MEMBER(data)
        END_JSON_PARSER

        /**
         * @brief const版本的JSON序列化
         * 宏生成的toJson()是非const方法，此处通过const_cast提供const版本
         */
        Json::Value ToJsonConst() const {
            ScmResponse &mutableThis = const_cast<ScmResponse &>(*this);  // NOLINT (readability-const-cast)
            return mutableThis.toJson();
        }
    };

}  // namespace qifeng::scm
