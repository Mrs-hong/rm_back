/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <grpc/grpc.h>

#include "common/config_define.h"
#include "rpc/grpc_server_manager.h"

namespace qifeng {

    // gRPC配置参数
    GrpcServerManager::GrpcConfig::GrpcConfig()
        : mPort(Grpc::DefaultGrpcPort), mThreadNum(Grpc::DefaultGrpcThreadNum),
          mMaxMessageSize(Grpc::DefaultGrpcMaxMessageSize), mKeepaliveTimeMs(Grpc::DefaultGrpcKeepaliveTime),
          mKeepaliveTimeoutMs(Grpc::DefaultGrpcKeepaliveTimeout),
          mEnableCompression(Grpc::DefaultGrpcEnableCompression), mAddress(std::string(Grpc::DefaultGrpcAddress)),
          mConfigManager(nullptr) {
    }

    void GrpcServerManager::GrpcConfig::LoadGrpcConfig() {
        if (!mConfigManager) {
            FLOG_WARN("Config manager not available, using default gRPC configuration");
            return;
        }

        mPort = mConfigManager->GetInt(std::string(Grpc::Section), std::string(Grpc::KeyPort), Grpc::DefaultGrpcPort);
        mThreadNum = mConfigManager->GetInt(std::string(Grpc::Section), std::string(Grpc::KeyThreadNum),
                                            Grpc::DefaultGrpcThreadNum);
        mMaxMessageSize = mConfigManager->GetInt(std::string(Grpc::Section), std::string(Grpc::KeyMaxMessageSize),
                                                 Grpc::DefaultGrpcMaxMessageSize);
        mKeepaliveTimeMs = mConfigManager->GetInt(std::string(Grpc::Section), std::string(Grpc::KeyKeepaliveTime),
                                                  Grpc::DefaultGrpcKeepaliveTime);
        mKeepaliveTimeoutMs = mConfigManager->GetInt(std::string(Grpc::Section), std::string(Grpc::KeyKeepaliveTimeout),
                                                     Grpc::DefaultGrpcKeepaliveTimeout);
        mEnableCompression = mConfigManager->GetBool(
            std::string(Grpc::Section), std::string(Grpc::KeyEnableCompression), Grpc::DefaultGrpcEnableCompression);
        mAddress = mConfigManager->GetString(std::string(Grpc::Section), std::string(Grpc::KeyAddress),
                                             std::string(Grpc::DefaultGrpcAddress));

        SLOG_INFO << "gRPC configuration loaded: port: " << mPort << ", threads: " << mThreadNum
                  << ", MaxMessageSize: " << mMaxMessageSize / (1024 * 1024)
                  << ", KeepaliveParams: " << mKeepaliveTimeMs << ", KeepaliveTimeout: " << mKeepaliveTimeoutMs
                  << ", EnableCompression: " << mEnableCompression << ", Address: " << mAddress;
    }

    GrpcServerManager::GrpcServerManager() : mIsInitialized(false), mIsRunning(false) {
        grpc_init();

        FLOG_INFO("GrpcServerManager constructor called");
    }

    GrpcServerManager::~GrpcServerManager() {
        Stop();

        grpc_shutdown();

        FLOG_INFO("GrpcServerManager destructor called");
    }

    GrpcServerManager& GrpcServerManager::GetInstance() {
        static GrpcServerManager Instance;
        return Instance;
    }

    bool GrpcServerManager::Initialize(const std::string& serviceName, ConfigManager& configManager) {
        if (mIsInitialized) {
            FLOG_WARN("GrpcServerManager already initialized");
            return true;
        }

        mServiceName = serviceName;
        mGrpcConfig.mConfigManager = &configManager;
        mServerBuilder = std::make_unique<grpc::ServerBuilder>();

        // 开启grpc的健康监控
        grpc::EnableDefaultHealthCheckService(true);

        // 加载配置
        mGrpcConfig.LoadGrpcConfig();
        // 初始化配置
        ConfigureServerOptions();

        mIsInitialized = true;
        FLOG_INFO("GrpcServerManager initialized successfully");
        return true;
    }

    bool GrpcServerManager::RegisterService(const std::string& serviceName, ServiceRegisterFunction registerFunc) {
        if (!mIsInitialized) {
            FLOG_ERROR("GrpcServerManager not initialized before registering service: " + serviceName);
            return false;
        }

        if (mServiceRegisterMap.find(serviceName) != mServiceRegisterMap.end()) {
            FLOG_WARN("Service already registered: " + serviceName);
            return true;
        }

        mServiceRegisterMap[serviceName] = registerFunc;

        if (mServerBuilder) {
            registerFunc(*mServerBuilder);
            FLOG_INFO("Service registered: " + serviceName);
        }

        return true;
    }

    bool GrpcServerManager::Run() {
        if (!mIsInitialized) {
            FLOG_ERROR("GrpcServerManager not initialized before starting");
            return false;
        }

        if (mIsRunning) {
            FLOG_WARN("GrpcServerManager already running");
            return true;
        }

        try {
            // Build and start the server
            mServer = mServerBuilder->BuildAndStart();
            if (!mServer) {
                FLOG_ERROR("Failed to build gRPC server");
                return false;
            }

            mIsRunning = true;
            FLOG_INFO("gRPC server started on " + mGrpcConfig.GetServerAddress());

            mServer->Wait();
            FLOG_INFO("gRPC server completed");
            return true;
        } catch (const std::exception& e) {
            SLOG_ERROR << "Exception when starting gRPC server: " << e.what();
            return false;
        } catch (...) {
            FLOG_ERROR("Unknown exception when starting gRPC server");
            return false;
        }
    }

    void GrpcServerManager::Stop() {
        if (mServer) {
            mServer->Shutdown();
            FLOG_INFO("gRPC server shutdown");
        }
        mIsRunning = false;
    }

    std::string GrpcServerManager::GrpcConfig::GetServerAddress() const {
        return mAddress + ":" + std::to_string(mPort);
    }

    void GrpcServerManager::ConfigureServerOptions() {
        if (!mServerBuilder) {
            return;
        }

        // 地址 & 端口
        std::string serverAddr = mGrpcConfig.mAddress + ":" + std::to_string(mGrpcConfig.mPort);
        mServerBuilder->AddListeningPort(serverAddr, grpc::InsecureServerCredentials());

        // 最大消息大小
        mServerBuilder->SetMaxReceiveMessageSize(mGrpcConfig.mMaxMessageSize);
        mServerBuilder->SetMaxSendMessageSize(mGrpcConfig.mMaxMessageSize);

        // Keepalive 配置
        grpc::ChannelArguments args;
        mServerBuilder->AddChannelArgument(GRPC_ARG_KEEPALIVE_TIME_MS, mGrpcConfig.mKeepaliveTimeMs);
        mServerBuilder->AddChannelArgument(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, mGrpcConfig.mKeepaliveTimeoutMs);
        mServerBuilder->AddChannelArgument(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, true);

        // 线程数（对Callback模型不完全受控）
        mServerBuilder->SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::NUM_CQS, mGrpcConfig.mThreadNum);
        mServerBuilder->SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::MIN_POLLERS, mGrpcConfig.mThreadNum);
        mServerBuilder->SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::MAX_POLLERS, mGrpcConfig.mThreadNum);

        // 压缩
        if (mGrpcConfig.mEnableCompression) {
            mServerBuilder->SetDefaultCompressionAlgorithm(GRPC_COMPRESS_GZIP);
        }
    }
}  // namespace qifeng
