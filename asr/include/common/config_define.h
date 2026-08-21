/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_COMMON_CONFIG_DEFINE_H
#define QIFENG_FRAMEWORK_INCLUDE_COMMON_CONFIG_DEFINE_H

#include <string_view>

// ===================== Log =====================
namespace Log {
    // 日志相关默认值
    constexpr static int DefaultMaxFileSize = 100 * 1024 * 1024;      // 默认100MB
    constexpr static int DefaultMaxFiles = 30;                        // 默认保留30个文件
    constexpr static int DefaultLogQueueSize = 8192;                  // 默认异步线程队列大小
    constexpr static int DefaultLogThreadCnt = 1;                     // 默认异步线程池大小
    constexpr static std::string_view DefaultLogDir = "logs";         // 默认日志目录
    constexpr static std::string_view DefaultLogFileSuffix = ".log";  // 默认日志文件名
    static constexpr std::string_view DefaultLogFileFormat = "[%Y-%m-%d %H:%M:%S.%e][%l][%P|%t] %v";  // 默认日志格式
    constexpr static int DefaultLogFlushInterval = 30;  // 默认30秒刷新一次
}  // namespace Log

// ===================== General =====================
namespace General {
    inline constexpr std::string_view Section = "general";

    inline constexpr std::string_view KeyLogLevel = "log_level";
    inline constexpr int ConstraintLogLevelMin = 0;
    inline constexpr int ConstraintLogLevelMax = 6;
}  // namespace General

// ===================== Database =====================
namespace Database {
    inline constexpr std::string_view Section = "database";

    // sqlite
    inline constexpr std::string_view KeySqliteEnable = "sqlite_enable";
    inline constexpr std::string_view KeySqliteDbPath = "sqlite_db_path";
    inline constexpr std::string_view DefaultSqliteDbPath = "./agent.db";

    // postgres
    inline constexpr std::string_view KeyPostgresEnable = "postgres_enable";
    inline constexpr std::string_view KeyPostgresServer = "postgres_server";
    inline constexpr std::string_view KeyPostgresPort = "postgres_port";
    inline constexpr std::string_view KeyPostgresDb = "postgres_db";
    inline constexpr std::string_view KeyPostgresUser = "postgres_user";
    inline constexpr std::string_view KeyPostgresPassword = "postgres_password";
}  // namespace Database

// ===================== Service =====================
namespace Service {
    inline constexpr std::string_view Section = "service";

    inline constexpr std::string_view KeyName = "name";

    inline constexpr std::string_view KeyRegisterType = "register_type";
    inline constexpr std::string_view DefaultRegisterType = "local";

    inline constexpr std::string_view KeyPortStart = "port_start";
    inline constexpr int DefaultPortStart = 25001;
    inline constexpr int ConstraintPortStartMin = 1;
    inline constexpr int ConstraintPortStartMax = 65535;

    inline constexpr std::string_view KeyPortEnd = "port_end";
    inline constexpr int DefaultPortEnd = 25010;
    inline constexpr int ConstraintPortEndMin = 1;
    inline constexpr int ConstraintPortEndMax = 65535;

    // file
    inline constexpr std::string_view KeyFileUrlPrefix = "file_url_prefix";
    inline constexpr std::string_view DefaultFileUrlPrefix = "https://ga-api.dev.qifeng.ai/";

    inline constexpr std::string_view KeyFileStoragePath = "file_storage_path";
    inline constexpr std::string_view DefaultFileStoragePath = "./uploaded_files";

    // 服务发现相关定义
    inline constexpr std::string_view ActiveStatus = "ACTIVE";      // 服务激活状态
    inline constexpr std::string_view InActiveStatus = "INACTIVE";  // 服务非激活状态
}  // namespace Service

// ===================== Models =====================
namespace Models {
    inline constexpr std::string_view Section = "models";

    inline constexpr std::string_view KeyLlmModel = "llm_model";
    inline constexpr std::string_view DefaultLlmModel = "gpt-3.5-turbo";

    inline constexpr std::string_view KeyLlmApiKey = "llm_api_key";
    inline constexpr std::string_view DefaultLlmApiKey = "";

    inline constexpr std::string_view KeyLlmApiBase = "llm_api_base";
    inline constexpr std::string_view DefaultLlmApiBase = "https://api.openai.com/v1";
}  // namespace Models

// ===================== SSH =====================
namespace Ssh {
    inline constexpr std::string_view Section = "ssh";

    inline constexpr std::string_view KeyUser = "user";
    inline constexpr std::string_view DefaultUser = "";

    inline constexpr std::string_view KeyIp = "ip";
    inline constexpr std::string_view DefaultIp = "";

    inline constexpr std::string_view KeyPrivateKeyPath = "private_key_path";
    inline constexpr std::string_view DefaultPrivateKeyPath = "";

    inline constexpr std::string_view KeyConnectionTimeout = "connection_timeout";
    inline constexpr int DefaultConnectionTimeout = 30000;

    inline constexpr std::string_view KeyExecutionTimeout = "execution_timeout";
    inline constexpr int DefaultExecutionTimeout = 60000;
}  // namespace Ssh

// ===================== Auth =====================
namespace Auth {
    inline constexpr std::string_view Section = "auth";

    inline constexpr std::string_view KeyAdminKey = "adminKey";
    inline constexpr std::string_view DefaultAdminKey = "";
}  // namespace Auth

// ===================== Grpc =====================
namespace Grpc {
    inline constexpr std::string_view Section = "grpc";

    inline constexpr std::string_view KeyPort = "port";
    inline constexpr int DefaultGrpcPort = 50051;  // 默认gRPC服务端口

    inline constexpr std::string_view KeyThreadNum = "thread_num";
    inline constexpr int DefaultGrpcThreadNum = 8;  // 默认gRPC线程数(对CallBack模型不生效)

    inline constexpr std::string_view KeyMaxMessageSize = "max_message_size";
    inline constexpr int DefaultGrpcMaxMessageSize = 10 * 1024 * 1024;  // 默认最大消息大小(10MB)

    inline constexpr std::string_view KeyKeepaliveTime = "keepalive_time";
    inline constexpr int DefaultGrpcKeepaliveTime = 7200 * 1000;  // 默认保活时间(2小时，单位：秒)

    inline constexpr std::string_view KeyKeepaliveTimeout = "keepalive_timeout";
    inline constexpr int DefaultGrpcKeepaliveTimeout = 20 * 1000;  // 默认保活超时(20秒)

    inline constexpr std::string_view KeyEnableCompression = "enable_compression";
    inline constexpr bool DefaultGrpcEnableCompression = true;  // 默认启用压缩

    inline constexpr std::string_view KeyAddress = "address";
    inline constexpr std::string_view DefaultGrpcAddress = "0.0.0.0";  // 默认监听地址
}  // namespace Grpc

// ===================== WebSocket =====================
namespace WebSocket {
    inline constexpr std::string_view Section = "websocket";

    inline constexpr std::string_view KeyPort = "port";
    inline constexpr int DefaultPort = 11452;

    inline constexpr std::string_view KeyPartialIntervalMs = "partial_interval_ms";
    inline constexpr int DefaultPartialIntervalMs = 1000;

    inline constexpr std::string_view KeyFinalIntervalMs = "final_interval_ms";
    inline constexpr int DefaultFinalIntervalMs = 2000;
}  // namespace WebSocket

// ===================== Http =====================
namespace Http {
    inline constexpr std::string_view Section = "http";

    inline constexpr std::string_view KeyPort = "port";
    inline constexpr int DefaultPort = 18081;
}  // namespace Http

namespace Dag {
    inline constexpr std::string_view Section = "dag";

    inline constexpr std::string_view KeyComputeThreads = "compute_threads";
    inline constexpr int DefaultComputeThreads = 4;
    inline constexpr int ConstraintComputeThreadsMin = -1;  // 最小值
    inline constexpr int ConstraintComputeThreadsMax = 16;  // 最大值
}  // namespace Dag

namespace Aas {
    inline constexpr std::string_view Section = "aas";

    inline constexpr std::string_view KeySampleRate = "sample_rate";
    inline constexpr int DefaultSampleRate = 16000;
    inline constexpr int ConstraintSampleRateMin = 16000;  // 最小值
    inline constexpr int ConstraintSampleRateMax = 48000;  // 最大值

    inline constexpr std::string_view KeyChannels = "channels";
    inline constexpr int DefaultChannels = 1;
    inline constexpr int ConstraintChannelsMin = 1;  // 最小值
    inline constexpr int ConstraintChannelsMax = 2;  // 最大值

    inline constexpr std::string_view KeyBitDepth = "bit_depth";
    inline constexpr int DefaultBitDepth = 32;
    inline constexpr int ConstraintBitDepthMin = 16;  // 最小值
    inline constexpr int ConstraintBitDepthMax = 32;  // 最大值
}  // namespace Aas

const static std::string_view TimerFormat = "%Y-%m-%d %H:%M:%S";
inline constexpr static std::string_view PermissionDenyMsg = "Permission denied";  // 权限拒绝消息

#endif  //  QIFENG_FRAMEWORK_INCLUDE_COMMON_CONFIG_DEFINE_H
