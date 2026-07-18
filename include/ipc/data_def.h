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
        SLOG,         // slog [--name] [--count] 查看服务日志（--name 省略时查 scmd 自身）
        CHECK,        // check 设备自检
        INIT_NGINX,   // init_nginx -n <name> -d <path> 独立配置服务的 nginx
        RESET_NGINX,  // reset_nginx -n <name> 重置服务的 nginx 配置
        ADD_MODEL,    // add_model <path> 安装/升级模型文件
        CLEAR_MODEL,  // clear_model -n <name> 停用并备份模型
        UPGRADES      // upgrades -n <name> 使用服务内部预置升级包升级并写入结果文件
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
        std::string serviceName;  // 服务名称（空表示 scmd 自身，读取 qifeng-scm.log）
        int logCount {0};         // 日志行数
    };

    struct CheckRequest {
        std::string configPath;  // 自检配置文件路径（可选，为空则使用默认路径）
    };

    struct InitNginxRequest {
        std::string dirPath;      // nginx 配置源路径（目录或 tar.gz）
    };

    /**
     * @brief nginx 重置模式
     * @details WAIT: 等待状态（所有路由返回 404，用于服务升级期间）
     *          NORMAL: 恢复正常（使 scm_*.conf 生效，移除 waiting 配置）
     *          BACK: 全部无效（只能访问 nginx 欢迎页，移除所有 scm 配置和系统默认站点）
     */
    enum class NginxResetMode {
        WAIT,    // 等待状态：安装 waiting.conf，所有路由返回 404
        NORMAL,  // 恢复正常：移除 waiting.conf，恢复 scm_*.conf
        BACK     // 全部无效：移除所有 scm 配置，恢复系统默认欢迎页
    };

    struct ResetNginxRequest {
        NginxResetMode mode {NginxResetMode::BACK};
    };

    /**
     * @brief 模型安装请求
     * @details 输入可以是目录或 tar/tar.gz 包路径；
     *          tar 包以解压后顶层目录名为模型名，目录则以 basename 为模型名。
     */
    struct AddModelRequest {
        std::string srcPath;  // 模型源路径（目录或 tar/tar.gz 包）
    };

    /**
     * @brief 模型停用请求
     * @details 将指定模型文件/目录重命名为 <name>.back，并验证依赖服务无影响。
     */
    struct ClearModelRequest {
        std::string modelName;  // 模型名（model_dir 下的文件或目录名）
    };

    /**
     * @brief 一体化升级请求（服务+模型+Nginx）
     * @details 由 upgrades 命令使用。tarDir 为空时从服务配置的 upgrade.soft_dir 查找素材，
     *          非空时从指定目录/tar包查找素材。支持三类素材：
     *          服务包(<serviceName>*.tar.gz)、模型(model*前缀)、nginx配置(nginx*前缀目录)。
     */
    struct UpgradesRequest {
        std::string serviceName;  // 服务名称
        std::string tarDir;       // 外部升级素材目录/tar包路径（空表示使用服务内部soft_dir）
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
        CheckRequest,
        InitNginxRequest,
        ResetNginxRequest,
        AddModelRequest,
        ClearModelRequest,
        UpgradesRequest
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
                if constexpr (std::is_same_v<T, InitNginxRequest>) return ScmCommand::INIT_NGINX;
                if constexpr (std::is_same_v<T, ResetNginxRequest>) return ScmCommand::RESET_NGINX;
                if constexpr (std::is_same_v<T, AddModelRequest>) return ScmCommand::ADD_MODEL;
                if constexpr (std::is_same_v<T, ClearModelRequest>) return ScmCommand::CLEAR_MODEL;
                if constexpr (std::is_same_v<T, UpgradesRequest>) return ScmCommand::UPGRADES;
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
