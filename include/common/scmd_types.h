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

        // ResultMsg前向声明
        struct ResultMsg;

        // 辅助函数声明
        ResultMsg MakeResult(int code, const std::string &msg);
        // code: 0 成功
        ResultMsg MakeSuccess();
        // code: -1 失败
        ResultMsg MakeError(const std::string &msg);
        // code: 1 警告
        ResultMsg MakeWarning(const std::string &msg);

        /**
         * @brief 操作结果封装
         */
        struct ResultMsg {
            int code {0};     // 0: success, non-zero: failure
            std::string msg;  // 错误信息

            ResultMsg() = default;
            ResultMsg(int c, const std::string &m) : code(c), msg(m) {}

            bool IsDefalutSuccess() const { return code == 0; }
        };

        /**
         * @brief 数据库类型枚举
         */
        enum class DatabaseType { MYSQL, OPENGAUSS };

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
        };

        /**
         * @brief 健康检查信息
         */
        struct HealthCheckInfo {
            uint32_t intervalSec;    // 健康检查间隔时间（秒）
            uint32_t timeoutSec;     // 健康检查超时时间（秒）
            uint32_t retries;        // 健康检查重试次数
            std::string httpUrl;     // 健康检查Http URL
            int expectedHttpStatus;  // 预期的Http状态码
        };

        /**
         * @brief 数据库信息
         */
        struct DataBaseInfo {
            DatabaseType dbType {DatabaseType::MYSQL};  // 数据库类型
            std::string sqlDir;                         // 数据库初始化SQL脚本目录
        };

        struct ResourcesInfo {
            int memoryMB {0};        // 内存占用（MB）
            int cpuPercent {0};      // CPU占用（%）
            std::vector<int> ports;  // 服务监听的端口号
        };

        /**
         * @brief 服务定义
         */
        struct ServiceDefinition {
            std::string serviceName;                          // 服务名称
            std::string version;                              // 服务版本号
            pid_t pid {0};                                    // 服务启动后的进程ID
            bool isAutoStart {false};                         // 是否自动启动
            DataBaseInfo dbInfo;                              // 数据库信息
            ExecutionInfo execInfo;                           // 服务执行信息
            ResourcesInfo resourcesInfo;                      // 资源信息
            HealthCheckInfo healthCheckInfo;                  // 健康检查信息
            std::map<std::string, std::string> dependencies;  // 服务依赖的其他服务ID，格式为<服务名称,版本号>
            std::string currentServiceDir;                    // 当前服务的服务目录
            bool useful {false};                              // 是否有用
        };

        /**
         * @brief 服务运行时信息
         */
        struct ServiceRuntimeInfo {
            int serviceId {0};                // 服务ID
            pid_t pid {0};                    // 服务进程ID
            std::string currentVersion;       // 当前运行的服务版本号
            std::string status;               // 服务状态（如已安装、运行中、已停止等）
            std::string startTime;            // 服务启动时间: 年月日时分秒毫秒
            std::string runTime;              // 服务运行时间：天、时、分、秒、毫秒
            size_t memoryUsage {0};           // 服务内存占用（字节）
            size_t cpuUsage {0};              // 服务CPU占用（%）
            std::string configFilePath;       // 服务配置文件路径
            std::string logFilePath;          // 服务日志文件路径
            std::string dbFilePath;           // 数据库文件路径
            int recoveryCount {0};            // 重启尝试次数
            std::string lastHealthCheckTime;  // 上次健康检查时间
        };

        /**
         * @brief 服务状态枚举
         */
        enum class ServiceStatus { UNKNOWN, INSTALLED, STOPPED, RUNNING, FAILED, ERROR };

        /**
         * @brief 日志级别枚举
         */
        enum class LogLevel { DEBUG, INFO, WARNING, ERROR, FATAL };

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

            // 健康检查配置
            uint32_t healthCheckIntervalSec;  // 健康检查间隔时间（秒）
            uint32_t healthCheckTimeoutSec;   // 健康检查超时时间（秒）
            uint32_t healthCheckRetries;      // 健康检查重试次数
            bool healthCheckEnabled;          // 是否启用健康检查

            // 操作超时配置
            uint32_t optTimeoutSec;  // 启动、停止、安装、卸载、升级超时时间（秒）

            // 关键文件目录
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
