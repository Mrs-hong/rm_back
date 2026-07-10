/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace qifeng {
    namespace scm {

        /**
         * @brief 服务状态枚举
         */
        enum class ServiceStatus { UNKNOWN, INSTALLED, STOPPED, RUNNING, FAILED, ERROR };

        /**
         * @brief 日志级别枚举
         */
        enum class LogLevel { DEBUG, INFO, WARNING, ERROR, FATAL };

        /**
         * @brief 数据库类型枚举
         */
        enum class DatabaseType { MYSQL, OPENGAUSS, NONE };

        /**
         * @brief 执行配置信息
         */
        struct ExecutionInfo {
            std::string command;            // 可执行文件名
            std::vector<std::string> args;  // 命令行参数
            std::string workDir;            // 工作目录
            std::string dataDir;            // 数据库和用户数据的目录
            std::vector<std::string> env;   // 环境变量
            std::string user;               // 运行用户
            int gracefulStopSignal {15};    // Linux signal number (default SIGTERM)
            uint32_t timeoutStopSec {10};  // systemd 停止服务超时时间（秒），超时后 systemd 发送 SIGKILL
        };

        /**
         * @brief 数据库信息
         */
        struct DataBaseInfo {
            DatabaseType dbType {DatabaseType::NONE};  // 数据库类型
            std::string sqlDir;                        // 数据库初始化SQL脚本目录
            std::string outputDir;                     // 存放创建用户和密码的文件目录
        };

        struct ResourcesInfo {
            int memoryMB {0};        // 内存占用（MB）
            int cpuPercent {0};      // CPU占用（%）
            std::vector<int> ports;  // 服务监听的端口号
        };

        /**
         * @brief 服务升级配置（来自 service.yaml 的 upgrade 段）
         */
        struct UpgradeConfig {
            std::string softDir;     // 升级软件包存放目录（相对路径，基于 currentServiceDir 解析）
            std::string resultPath;  // 升级结果文件路径（相对路径，基于 currentServiceDir 解析）
        };

        /**
         * @brief 服务定义
         */
        struct ServiceDefinition {
            std::string serviceName;                          // 服务名称
            std::string version;                              // 服务版本号
            pid_t pid {0};                                    // 服务启动后的进程ID
            bool isAutoStart {false};                         // 是否自动启动
            DataBaseInfo dbInfo;                              // 依赖的数据库信息
            ExecutionInfo execInfo;                           // 服务执行信息
            ResourcesInfo resourcesInfo;                      // 资源信息
            std::map<std::string, std::string> dependencies;  // 服务依赖的其他服务ID，格式为<服务名称,版本号>
            std::string currentServiceDir;                    // 当前服务的服务目录
            bool isUseful {false};                            // 是否有用
            bool needModel {false};                           // 是否依赖模型文件（来自 service.yaml 的 need_model）
            std::string modelLinkDir;                         // 模型文件软链接路径（相对路径，基于 currentServiceDir，来自 service.yaml 的 model_link_dir）
            uint32_t keepAliveTimeSec {3};                    // 安装/模型变更后服务需保持运行的验证时长（秒），0 表示不验证，范围 [0, 30]
            UpgradeConfig upgradeConfig;                      // 升级配置（soft_dir、result_path）
        };

        /**
         * @brief 服务运行时信息
         */
        struct ServiceRuntimeInfo {
            pid_t pid {0};               // 服务进程ID
            std::string currentVersion;  // 当前运行的服务版本号
            std::string status;          // 服务状态（如已安装、运行中、已停止等）
            std::string startTime;       // 服务启动时间: 年月日时分秒毫秒
            std::string runTime;         // 服务运行时间：天、时、分、秒、毫秒
            size_t memoryUsage {0};      // 服务内存占用（字节）
            size_t cpuUsage {0};         // 服务CPU占用（%）
            std::string configFilePath;  // 服务配置文件路径
            std::string rootPath;        // 服务安装根路径
            std::string dbFilePath;      // 数据库文件路径
            int recoveryCount {0};       // 重启尝试次数
            std::string subState;        // systemd SubState（running/dead/exited/failed/auto-restart）
            std::string errorResult;  // systemd execution Result（success/exit-code/signal/timeout/start-limit-hit 等）
            int exitCode {0};         // 进程退出码
            int exitStatus {0};       // 进程退出状态（被信号杀死时为信号编号）
        };

        /**
         * @brief scmd配置信息
         */
        struct ConfigInfo {
            // 日志
            LogLevel logLevel;       // 日志级别
            uint32_t logFileSizeMB;  // 日志文件大小（MB）
            uint32_t logFileCount;   // 日志文件数量

            // uds配置
            std::string udsSocketPath;  // uds socket路径
            int udsSocketMode {0666};   // uds socket文件权限（默认0666允许所有用户连接）

            // 操作超时配置
            uint32_t optTimeoutSec;  // 启动、停止、安装、卸载、升级超时时间（秒）

            // 根目录，所有子目录基于此派生
            std::string rootDir;
            // 关键文件目录（由 rootDir 派生）
            // .cofig 是固定和.exe在同一目录下的配置文件
            std::string configDir;
            // .service 服务文件目录
            std::string serviceDir;
            // .data 数据目录，数据库和用户数据的目录
            std::string dataDir;
            // .backup 备份目录
            std::string backupDir;
            // .logs 日志目录
            std::string logsDir;
            // .temp 临时目录
            std::string tempDir;

            // 自检配置
            bool selftestEnabled {true};              // 是否启用开机自检
            std::string selftestConfigPath;           // 自检配置文件路径（为空则使用 configDir/selftest.json）
            std::string selftestFailAction {"warn"};  // 自检失败动作：warn（仅告警）或 halt（阻止启动）

            // 模型文件配置
            std::string modelDir;     // 模型文件存储目录（绝对路径）
            std::string modelEnvVar;  // 注入到依赖模型服务的环境变量名（值=模型目录绝对路径）
        };

        /**
         * @brief 服务启动/停止序列
         */
        struct ServiceSequence {
            std::vector<std::string> startOrder;  // 启动顺序（被依赖的在前）
            std::vector<std::string> stopOrder;   // 停止顺序（依赖别人的在前，即启动顺序的逆序）
            // 反向邻接表：reverseAdj[A] = {B, C} 表示 B 和 C 依赖 A
            // 用于快速查找依赖指定服务的所有服务（GetDependentServices）
            std::map<std::string, std::vector<std::string>> reverseAdj;
        };

        struct CheckDependencyError {
            /**
             * @description: 依赖检查错误状态枚举
             *  VERSION_CONFLICT: 版本冲突
             *  MISSING: 缺少依赖
             *  CIRCULAR: 循环依赖
             *  IMPACTED: 受间接依赖影响的服务
             */
            enum class Status { VERSION_CONFLICT, MISSING, CIRCULAR, IMPACTED };
            Status status;
            std::string serviceName;
        };
    }  // namespace scm
}  // namespace qifeng
