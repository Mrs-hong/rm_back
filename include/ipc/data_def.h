/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <json/json.h>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
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

    // =========================================================================
    // 请求字段反射基础设施
    // 通过 ScmFieldDesc + ScmRequestTraits 实现统一的序列化/反序列化，
    // 消除每个命令手写的 ParamsToJson/ParamsFromJson 重载。
    // =========================================================================

    /**
     * @brief 字段描述：成员指针 + json key + 是否必填
     * @details 反序列化时：
     *          - required=true：字段缺失或类型不匹配视为反序列化失败
     *          - required=false：字段缺失或类型不匹配时跳过（保留成员默认值）
     */
    template <typename ClassType, typename MemberType>
    struct ScmFieldDesc {
        MemberType ClassType::*ptr;  // 成员指针
        const char* key;             // json 键名
        bool required;               // 是否必填
    };

    /**
     * @brief 请求类型特征（主模板不定义，强制每个请求类型显式特化）
     * @details 每个特化需提供：
     *          - kCommand：对应的 ScmCommand 枚举值
     *          - kFields：字段描述 tuple（ScmFieldDesc 的列表，可为空 tuple）
     */
    template <typename T>
    struct ScmRequestTraits;

    /**
     * @brief 声明 ScmRequestTraits 特化
     * @param RequestType 请求结构体类型
     * @param CmdEnum 对应的 ScmCommand 枚举值
     * @param ... 逗号分隔的 SCM_FIELD 列表
     * @note 使用时需在末尾加分号；无字段请求请使用 SCM_DEFINE_REQUEST_EMPTY
     */
    #define SCM_DEFINE_REQUEST(RequestType, CmdEnum, ...) \
        template <> \
        struct ScmRequestTraits<RequestType> { \
            static constexpr ScmCommand kCommand = CmdEnum; \
            static constexpr auto kFields = std::make_tuple(__VA_ARGS__); \
        }

    /**
     * @brief 为无字段的请求结构体声明 ScmRequestTraits 特化
     * @param RequestType 请求结构体类型
     * @param CmdEnum 对应的 ScmCommand 枚举值
     * @note 用于 VersionRequest/KillRequest 等无参数请求，避免可变参数宏空调用
     */
    #define SCM_DEFINE_REQUEST_EMPTY(RequestType, CmdEnum) \
        template <> \
        struct ScmRequestTraits<RequestType> { \
            static constexpr ScmCommand kCommand = CmdEnum; \
            static constexpr auto kFields = std::make_tuple(); \
        }

    /**
     * @brief 构造一个 ScmFieldDesc 字段描述
     * @param Class 请求结构体类型
     * @param Member 成员名
     * @param Key json 键名（字符串字面量）
     * @param Required 是否必填
     */
    #define SCM_FIELD(Class, Member, Key, Required) \
        ScmFieldDesc<Class, decltype(std::declval<Class>().Member)>{&Class::Member, Key, Required}

    /**
     * @brief 命令请求参数联合体
     * 每个命令对应一种参数类型，避免所有命令字段混用
     * @note variant 类型顺序与 ScmCommand 枚举值顺序保持一致，
     *       因此 variant::index() 与 static_cast<int>(ScmCommand) 一一对应，
     *       反序列化时可用枚举值直接作为查表索引。
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

    // =========================================================================
    // 21 个请求类型的 ScmRequestTraits 特化
    // 顺序与 ScmCommand 枚举值、ScmRequestData variant 类型顺序保持一致。
    // =========================================================================

    // 无字段的请求：kFields 为空 tuple
    SCM_DEFINE_REQUEST_EMPTY(VersionRequest, ScmCommand::VERSION);
    SCM_DEFINE_REQUEST(InstallRequest, ScmCommand::INSTALL,
        SCM_FIELD(InstallRequest, serviceName, "serviceName", true),
        SCM_FIELD(InstallRequest, tarDir, "tarDir", false));
    SCM_DEFINE_REQUEST(StartRequest, ScmCommand::START,
        SCM_FIELD(StartRequest, serviceName, "serviceName", true));
    SCM_DEFINE_REQUEST(StopRequest, ScmCommand::STOP,
        SCM_FIELD(StopRequest, serviceName, "serviceName", true));
    SCM_DEFINE_REQUEST(RestartRequest, ScmCommand::RESTART,
        SCM_FIELD(RestartRequest, serviceName, "serviceName", true));
    SCM_DEFINE_REQUEST_EMPTY(RestartAllRequest, ScmCommand::RESTART_ALL);
    SCM_DEFINE_REQUEST(UpgradeRequest, ScmCommand::UPGRADE,
        SCM_FIELD(UpgradeRequest, serviceName, "serviceName", true),
        SCM_FIELD(UpgradeRequest, tarDir, "tarDir", false));
    SCM_DEFINE_REQUEST_EMPTY(ListRequest, ScmCommand::LIST);
    SCM_DEFINE_REQUEST(InfoRequest, ScmCommand::INFO,
        SCM_FIELD(InfoRequest, serviceName, "serviceName", true),
        SCM_FIELD(InfoRequest, infoDetail, "infoDetail", false));
    SCM_DEFINE_REQUEST(LogRequest, ScmCommand::LOG,
        SCM_FIELD(LogRequest, logLevel, "logLevel", false),
        SCM_FIELD(LogRequest, logCount, "logCount", false));
    SCM_DEFINE_REQUEST(UninstallRequest, ScmCommand::UNINSTALL,
        SCM_FIELD(UninstallRequest, serviceName, "serviceName", true));
    SCM_DEFINE_REQUEST(ReloadRequest, ScmCommand::RELOAD,
        SCM_FIELD(ReloadRequest, serviceName, "serviceName", true));
    SCM_DEFINE_REQUEST_EMPTY(ReloadAllRequest, ScmCommand::RELOAD_ALL);
    SCM_DEFINE_REQUEST_EMPTY(KillRequest, ScmCommand::KILL);
    SCM_DEFINE_REQUEST(SlogRequest, ScmCommand::SLOG,
        SCM_FIELD(SlogRequest, serviceName, "serviceName", true),
        SCM_FIELD(SlogRequest, logCount, "logCount", false));
    SCM_DEFINE_REQUEST(CheckRequest, ScmCommand::CHECK,
        SCM_FIELD(CheckRequest, configPath, "configPath", false));
    SCM_DEFINE_REQUEST(InitNginxRequest, ScmCommand::INIT_NGINX,
        SCM_FIELD(InitNginxRequest, dirPath, "dirPath", false));
    SCM_DEFINE_REQUEST(ResetNginxRequest, ScmCommand::RESET_NGINX,
        SCM_FIELD(ResetNginxRequest, mode, "mode", false));
    SCM_DEFINE_REQUEST(AddModelRequest, ScmCommand::ADD_MODEL,
        SCM_FIELD(AddModelRequest, srcPath, "srcPath", true));
    SCM_DEFINE_REQUEST(ClearModelRequest, ScmCommand::CLEAR_MODEL,
        SCM_FIELD(ClearModelRequest, modelName, "modelName", true));
    SCM_DEFINE_REQUEST(UpgradesRequest, ScmCommand::UPGRADES,
        SCM_FIELD(UpgradesRequest, serviceName, "serviceName", true),
        SCM_FIELD(UpgradesRequest, tarDir, "tarDir", false));

    /**
     * @brief scmctl请求结构体
     * 定义scmctl发送给scmd的请求数据
     */
    struct ScmRequest {
        ScmRequestData data;

        /**
         * @brief 根据当前参数推导命令类型
         * @return 对应的 ScmCommand 枚举值
         * @note 通过 ScmRequestTraits<T>::kCommand 查表，替代手写 if constexpr 链
         */
        ScmCommand Command() const {
            return std::visit([](const auto& param) -> ScmCommand {
                using T = std::decay_t<decltype(param)>;
                return ScmRequestTraits<T>::kCommand;
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
