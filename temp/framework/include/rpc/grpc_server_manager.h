/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_COMMON_GRPC_SERVER_MANAGER_H
#define QIFENG_FRAMEWORK_INCLUDE_COMMON_GRPC_SERVER_MANAGER_H

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include <grpcpp/grpcpp.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include "common/config_manager.h"
#include "common/logger.h"

// Forward declarations
namespace grpc {
    class Server;
    class ServerBuilder;
    class ServerContext;
}  // namespace grpc

namespace qifeng {
    class GrpcServerManager {
    public:
        using ServiceRegisterFunction = std::function<void(grpc::ServerBuilder&)>;

        static GrpcServerManager& GetInstance();

        bool Initialize(const std::string& serviceName, ConfigManager& configManager);

        // 注册gRPC服务
        bool RegisterService(const std::string& serviceName, ServiceRegisterFunction registerFunc);

        bool Run();

        void Stop();

    private:
        GrpcServerManager();
        GrpcServerManager(const GrpcServerManager&) = delete;
        GrpcServerManager(GrpcServerManager&&) = default;
        const GrpcServerManager& operator=(const GrpcServerManager&) = delete;
        GrpcServerManager& operator=(GrpcServerManager&&) = default;

        ~GrpcServerManager();

        void ConfigureServerOptions();

        struct GrpcConfig {
            int mPort;                      // 服务器端口
            int mThreadNum;                 // 线程数
            int mMaxMessageSize;            // 最大消息大小
            int mKeepaliveTimeMs;           // 保活时间(ms)
            int mKeepaliveTimeoutMs;        // 保活超时(ms)
            bool mEnableCompression;        // 是否启用压缩
            std::string mAddress;           // 服务器地址
            ConfigManager* mConfigManager;  // 配置管理指针

            GrpcConfig();
            void LoadGrpcConfig();
            std::string GetServerAddress() const;
        };

        bool mIsInitialized;                                  // 是否已初始化
        bool mIsRunning;                                      // 是否已启动
        std::string mServiceName;                             // 服务名称
        GrpcConfig mGrpcConfig;                               // rpc配置
        std::unique_ptr<grpc::Server> mServer;                // gRPC服务器实例
        std::unique_ptr<grpc::ServerBuilder> mServerBuilder;  // gRPC服务器构建器
        // 服务注册函数映射
        std::unordered_map<std::string, ServiceRegisterFunction> mServiceRegisterMap;
    };

    template <typename ServiceImpl>
    void RunRpcServer(const std::string& serviceName) {
        auto& grpcManager = GrpcServerManager::GetInstance();
        auto& cfg = ConfigManager::GetInstance();
        if (!grpcManager.Initialize(serviceName, cfg)) {
            FLOG_ERROR("Failed to initialize gRPC server manager");
        }

        ServiceImpl service;
        grpcManager.RegisterService(serviceName,
                                    [&service](grpc::ServerBuilder& builder) { builder.RegisterService(&service); });

        // 启动 gRPC 服务器
        if (!grpcManager.Run()) {
            FLOG_ERROR("Failed to start gRPC server");
        }
    }
}  // namespace qifeng
#endif  // QIFENG_FRAMEWORK_INCLUDE_COMMON_GRPC_SERVER_MANAGER_H
