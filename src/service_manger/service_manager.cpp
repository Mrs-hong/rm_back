/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/types.h"
#include "service_manger/service_manager.h"

#include "common/config.h"
#include "common/scmd_def.h"
#include "common/scmd_types.h"
#include "common/utils.h"
#include "common/utils/file.h"
#include "common/utils/string.h"
#include "common/utils/systemd_time.h"
#include "common/utils/yaml_resolve.h"
#include "qifeng_framework/common/logger.h"
#include "service_manger/dbus_manager.h"
#include "service_manger/file_manager.h"
#include "service_manger/service_generator.h"
#include "service_tool/tool_mariadb.h"
#include "service_tool/tool_nginx.h"
#include "service_tool/tools_def.h"
#include <yaml-cpp/yaml.h>

#include "jsoncpp/json/json.h"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace qifeng::scm {

    ServiceManager::ServiceManager(std::shared_ptr<ConfigLoader> configLoader)
        : mConfigLoader(std::move(configLoader)), mFileManager(nullptr), mDBusManager(nullptr), mServiceSequence {},
          mInitialized(false), mSequenceDirty(true) {
        if (!mConfigLoader) {
            SLOG_ERROR << "ConfigLoader is null in ServiceManager constructor";
            return;
        }

        auto result = InitializeFileManager();
        if (!result.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to initialize FileManager: " << result.msg;
            return;
        }

        result = InitializeDBusManager();
        if (!result.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to initialize DBusManager: " << result.msg;
            return;
        }

        mInitialized = true;
        SLOG_INFO << "ServiceManager initialized successfully";
    }

    ServiceManager::~ServiceManager() {
        SLOG_INFO << "ServiceManager destroyed";
    }

    // --- 初始化方法 ---

    ResultMsg ServiceManager::InitializeFileManager() {
        if (!mConfigLoader->IsInitialized()) {
            auto initResult = mConfigLoader->Initialize();
            if (!initResult.IsDefalutSuccess()) {
                return MakeError("Failed to initialize ConfigLoader: " + initResult.msg);
            }
        }

        // 直接使用 ConfigLoader 的配置路径，确保 FileManager 与 ConfigLoader 路径完全一致
        const auto &configInfo = mConfigLoader->GetConfigInfo();
        FileDirInfo dirConfig;
        dirConfig.configDir = configInfo.configDir;
        dirConfig.serviceDir = configInfo.serviceDir;
        dirConfig.dataDir = configInfo.dataDir;
        dirConfig.backupDir = configInfo.backupDir;
        dirConfig.logsDir = configInfo.logsDir;
        dirConfig.tempDir = configInfo.tempDir;

        mFileManager = std::make_unique<FileManager>(std::move(dirConfig));
        auto result = mFileManager->InitFileDir();
        if (!result.IsDefalutSuccess()) {
            return MakeError("Failed to initialize FileManager: " + result.msg);
        }

        return MakeSuccess();
    }

    ResultMsg ServiceManager::InitializeDBusManager() {
        mDBusManager = std::make_unique<DBusManager>(BusType::System);
        if (!mDBusManager->IsConnected()) {
            return MakeError("Failed to connect to systemd DBus");
        }

        // sd_bus_open_system成功不代表实际能通信，需做一次真实调用验证
        auto testResult = mDBusManager->ReloadDaemon();
        if (!testResult.IsDefalutSuccess()) {
            SLOG_WARN << "DBus与systemd通信失败: " << testResult.msg;
            if (geteuid() != 0) {
                SLOG_WARN << "请使用sudo启动scmd以获得systemd控制权限";
            }
            return MakeError("DBus system bus communication failed: " + testResult.msg);
        } else {
            SLOG_INFO << "DBus connectivity test passed";
        }

        return MakeSuccess();
    }

    // --- 辅助方法 ---

    std::string ServiceManager::ToSystemdUnitName(const std::string &serviceName) {
        return std::string(FileManager::GetServiceFilePrefix()) + serviceName;
    }

    ResultMsg ServiceManager::ExtractSoftwareTar(const std::string &tarPath, const std::string &extractDir,
                                                 const std::string &newName) {
        auto result = utils::ExtractTarWithCleanup(tarPath, extractDir);
        if (!result.IsDefalutSuccess()) {
            return result;
        }

        // 重命名解压后的第一个目录
        if (!newName.empty()) {
            utils::RenameFirstSubdirectory(extractDir, newName);
        }

        SLOG_INFO << "Extracted tar file to: " << extractDir;
        return MakeSuccess();
    }

    // 从 service.yaml 路径直接解析服务名（去掉冗余的 ConfigLoader 扫描逻辑）
    std::string ServiceManager::ResolveServiceName(const std::string &yamlPath) {
        return utils::ReadServiceName(yamlPath);
    }

    ResultMsg ServiceManager::CheckServiceDependencies(const std::string &serviceName) {
        auto* svc = mConfigLoader->GetServiceByName(serviceName);
        if (!svc) {
            return MakeError("Service not found: " + serviceName);
        }

        if (svc->dependencies.empty()) {
            return MakeSuccess();
        }

        // EnsureSequenceUpdated 内部已调用 CheckDependenciesMap 进行完整依赖检查
        // 若存在循环依赖，序列计算会失败，此处直接返回
        auto seqResult = EnsureSequenceUpdated();
        if (!seqResult.IsDefalutSuccess()) {
            return seqResult;
        }

        // 对当前服务做精确的缺失和版本冲突检查
        for (const auto &[depName, depVersion] : svc->dependencies) {
            // 系统服务暂时跳过
            if (tool::IsMariadbService(depName)) {
                continue;
            }
            auto* depSvc = mConfigLoader->GetServiceByName(depName);
            if (!depSvc) {
                return MakeError("Missing dependency for service " + serviceName + ": " + depName);
            }
            if (!utils::SatisfiesVersionConstraint(depSvc->version, depVersion)) {
                return MakeError("Dependency version conflict for service " + serviceName + ": " + depName +
                                 " expected " + depVersion + " but got " + depSvc->version);
            }
        }

        return MakeSuccess();
    }

    ResultMsg ServiceManager::StartDependentServices(const std::string &serviceName) {
        auto seqResult = EnsureSequenceUpdated();
        if (!seqResult.IsDefalutSuccess()) {
            return seqResult;
        }
        // 系统服务暂时不检测、直接跳过
        if (!tool::IsMariadbService(serviceName)) {
            return MakeSuccess();
        }

        auto* svc = mConfigLoader->GetServiceByName(serviceName);
        if (!svc) {
            return MakeError("Service not found: " + serviceName);
        }

        if (svc->dependencies.empty()) {
            return MakeSuccess();
        }

        // 使用缓存的启动序列，按依赖顺序启动
        for (const auto &name : mServiceSequence.startOrder) {
            if (name == serviceName) {
                break;
            }

            if (!svc->dependencies.count(name)) {
                continue;
            }

            auto stateResult = mDBusManager->GetUnitActiveState(ToSystemdUnitName(name));
            if (stateResult.IsDefalutSuccess() && stateResult.msg == "active") {
                continue;
            }

            auto startResult = mDBusManager->StartUnit(ToSystemdUnitName(name));
            if (!startResult.IsDefalutSuccess()) {
                return MakeError("Failed to start dependency service " + name + ": " + startResult.msg);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        return MakeSuccess();
    }

    ResultMsg ServiceManager::SetServiceUser(const std::string &serviceName) {
        auto* svc = mConfigLoader->GetServiceByName(serviceName);
        if (!svc) {
            return MakeError("Service not found: " + serviceName);
        }

        if (!svc->execInfo.user.empty()) {
            return MakeSuccess();
        }
        svc->execInfo.user = utils::GetCurrentUserName();

        SLOG_INFO << "Setting service user to: " << svc->execInfo.user;
        return MakeSuccess();
    }

    ResultMsg ServiceManager::GenerateAndCreateServiceFile(const std::string &serviceName) {
        auto* svc = mConfigLoader->GetServiceByName(serviceName);
        if (!svc) {
            return MakeError("Service not found: " + serviceName);
        }

        auto genResult = ServiceGenerator::GenerateContent(*svc, FileManager::GetServiceFilePrefix(),
                                                           mConfigLoader->GetConfigInfo());
        if (!genResult.IsDefalutSuccess()) {
            return MakeError("Failed to generate service file content: " + genResult.msg);
        }

        // 检查 service 文件是否已存在，若存在则更新
        auto fileResult = mFileManager->CreateServiceFile(genResult.msg, serviceName);
        if (!fileResult.IsDefalutSuccess()) {
            // 文件已存在时尝试更新
            fileResult = mFileManager->FreshServiceFile(genResult.msg, serviceName);
            if (!fileResult.IsDefalutSuccess()) {
                return MakeError("Failed to create/update service file: " + fileResult.msg);
            }
        }

        auto linkResult = mFileManager->FreshServiceSymlink(serviceName);
        if (!linkResult.IsDefalutSuccess()) {
            return MakeError("Failed to fresh/update service symlink: " + linkResult.msg);
        }

        auto reloadResult = mDBusManager->ReloadDaemon();
        if (!reloadResult.IsDefalutSuccess()) {
            return MakeError("Failed to reload systemd daemon: " + reloadResult.msg);
        }

        return MakeSuccess();
    }

    ServiceStatus ServiceManager::ConvertActiveStateToStatus(const std::string &activeState) {
        if (activeState == "active") {
            return ServiceStatus::RUNNING;
        } else if (activeState == "inactive") {
            return ServiceStatus::STOPPED;
        } else if (activeState == "failed") {
            return ServiceStatus::FAILED;
        } else if (activeState == "activating" || activeState == "deactivating") {
            return ServiceStatus::UNKNOWN;
        }
        return ServiceStatus::UNKNOWN;
    }

    // NOLINTNEXTLINE(readability-function-size,readability-function-cognitive-complexity)
    ServiceRuntimeInfo ServiceManager::CollectRuntimeInfo(const std::string &serviceName) {
        ServiceRuntimeInfo info;

        // --- 1. 静态信息 ---
        auto* svc = mConfigLoader->GetServiceByName(serviceName);
        if (!svc) {
            SLOG_ERROR << "Service not found: " << serviceName;
            return info;
        }

        info.currentVersion = svc->version;
        info.configFilePath = mFileManager->GetServiceConfigPath(serviceName);

        // 数据库文件路径：服务目录 + dataDir
        if (!svc->execInfo.dataDir.empty()) {
            info.dbFilePath = utils::JoinPath(svc->currentServiceDir, svc->execInfo.dataDir);
        }

        // 服务安装根路径
        if (!svc->currentServiceDir.empty()) {
            info.rootPath = svc->currentServiceDir;
        }

        // --- 2. DBus 运行时信息 ---
        std::string unitName = ToSystemdUnitName(serviceName);

        // 服务活跃状态
        auto stateResult = mDBusManager->GetUnitActiveState(unitName);
        if (stateResult.IsDefalutSuccess()) {
            info.status = stateResult.msg;
        }

        // 主进程 PID
        auto pidResult = mDBusManager->GetServiceMainPID(unitName);
        if (pidResult.IsDefalutSuccess()) {
            try {
                info.pid = static_cast<pid_t>(std::stoul(pidResult.msg));
            } catch (...) {
                info.pid = 0;
            }
        }

        // 重启次数
        auto nRestartsResult = mDBusManager->GetServiceNRestarts(unitName);
        if (nRestartsResult.IsDefalutSuccess()) {
            try {
                info.recoveryCount = std::stoi(nRestartsResult.msg);
            } catch (...) {
                info.recoveryCount = 0;
            }
        }

        // 内存使用量（systemd 在服务未运行时返回 UINT64_MAX，视为 0）
        auto memResult = mDBusManager->GetServiceMemoryCurrent(unitName);
        if (memResult.IsDefalutSuccess()) {
            try {
                uint64_t memValue = std::stoull(memResult.msg);
                info.memoryUsage = (memValue == UINT64_MAX) ? 0 : static_cast<size_t>(memValue);
            } catch (...) {
                info.memoryUsage = 0;
            }
        }

        // 启动时间与运行时长（从 ActiveEnterTimestamp 获取）
        uint64_t activeEnterUsec = 0;
        auto timestampResult = mDBusManager->GetUnitActiveEnterTimestamp(unitName);
        if (timestampResult.IsDefalutSuccess()) {
            try {
                activeEnterUsec = std::stoull(timestampResult.msg);
                if (activeEnterUsec > 0) {
                    info.startTime = utils::FormatTimestamp(activeEnterUsec);
                    info.runTime = utils::FormatDuration(activeEnterUsec);
                }
            } catch (...) {
                // 忽略解析错误
            }
        }

        // CPU 占用百分比
        auto cpuResult = mDBusManager->GetServiceCPUUsageNSec(unitName);
        if (cpuResult.IsDefalutSuccess() && activeEnterUsec > 0) {
            try {
                uint64_t cpuUsageNSec = std::stoull(cpuResult.msg);
                info.cpuUsage = utils::CalculateCpuUsage(cpuUsageNSec, activeEnterUsec);
            } catch (...) {
                info.cpuUsage = 0;
            }
        }

        // --- 3. 错误诊断信息（SubState + 失败原因） ---
        // 获取 SubState（无论服务是否 active 都采集）
        auto subStateResult = mDBusManager->GetUnitSubState(unitName);
        if (subStateResult.IsDefalutSuccess()) {
            info.subState = subStateResult.msg;
        }

        // 当服务不在 active 状态时，采集详细的错误诊断信息
        bool isActive = (info.status == "active" || info.status == "activating");
        if (!isActive) {
            auto resultResult = mDBusManager->GetServiceResult(unitName);
            if (resultResult.IsDefalutSuccess()) {
                info.errorResult = resultResult.msg;
            }

            auto exitCodeResult = mDBusManager->GetServiceExecMainCode(unitName);
            if (exitCodeResult.IsDefalutSuccess()) {
                try {
                    info.exitCode = std::stoi(exitCodeResult.msg);
                } catch (...) {
                    info.exitCode = 0;
                }
            }

            auto exitStatusResult = mDBusManager->GetServiceExecMainStatus(unitName);
            if (exitStatusResult.IsDefalutSuccess()) {
                try {
                    info.exitStatus = std::stoi(exitStatusResult.msg);
                } catch (...) {
                    info.exitStatus = 0;
                }
            }
        }

        return info;
    }

    void ServiceManager::CleanupTempDirectory(const std::string &dirPath) {
        auto result = utils::ForceDeleteDirectory(dirPath);
        if (result.IsDefalutSuccess()) {
            SLOG_INFO << "Cleaned up temp directory: " << dirPath;
        } else {
            SLOG_ERROR << "Failed to cleanup temp directory: " << dirPath << ", error: " << result.msg;
        }
    }

    // --- 序列管理方法 ---

    void ServiceManager::MarkSequenceDirty() {
        mSequenceDirty = true;
    }

    ResultMsg ServiceManager::EnsureSequenceUpdated() {
        if (!mSequenceDirty) {
            return MakeSuccess();
        }

        auto allServices = mConfigLoader->GetAllServices();
        std::map<std::string, ServiceDefinition> servicesMap;
        for (const auto &s : allServices) {
            servicesMap[s.serviceName] = s;
        }

        mServiceSequence = utils::ComputeServiceSequence(servicesMap);
        if (mServiceSequence.startOrder.empty() && !servicesMap.empty()) {
            return MakeError("Circular dependency detected, cannot compute service sequence");
        }

        mSequenceDirty = false;
        SLOG_INFO << "Service sequence updated, start order size: " << mServiceSequence.startOrder.size();
        return MakeSuccess();
    }

    std::vector<std::string> ServiceManager::GetDependentServices(const std::string &serviceName) {
        // 优先使用缓存的反向邻接表，避免每次线性扫描所有服务
        auto seqResult = EnsureSequenceUpdated();
        if (seqResult.IsDefalutSuccess()) {
            auto it = mServiceSequence.reverseAdj.find(serviceName);
            if (it != mServiceSequence.reverseAdj.end()) {
                return it->second;
            }
        }

        return {};
    }

    // --- 公共接口 ---
    // NOLINTBEGIN(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg ServiceManager::InstallService(const std::string &softwareTarPath, const std::string &serviceName) {
        SLOG_INFO << "Installing service from: " << softwareTarPath;

        if (!mInitialized) {
            return MakeError("ServiceManager is not initialized");
        }
        if (mConfigLoader->GetServiceByName(serviceName) != nullptr) {
            return MakeError("Service name " + serviceName + " already exists");
        }

        std::string extractDir = utils::GenerateTempDir(mFileManager->GetCurDirConfig().tempDir);
        ResultMsg result;
        std::string actualServiceName;
        // 准备源目录：tar 包则解压，已解压目录则直接拷贝到临时目录
        if (IsTarPackage(softwareTarPath)) {
            result = ExtractSoftwareTar(softwareTarPath, extractDir, serviceName);
            if (!result.IsDefalutSuccess()) {
                CleanupTempDirectory(extractDir);
                return result;
            }
            // 解析服务名：从解压后的 service.yaml 中读取
            std::string yamlPath = utils::JoinPath(extractDir, serviceName, DefaultServiceName);
            actualServiceName = ResolveServiceName(yamlPath);
            if (actualServiceName.empty()) {
                CleanupTempDirectory(extractDir);
                return MakeError("Cannot determine service name from package");
            }

            if (actualServiceName != serviceName) {
                SLOG_ERROR << "Service name in package is " << actualServiceName << ", using name " << serviceName;
                CleanupTempDirectory(extractDir);
                return MakeError("Service name in package is " + actualServiceName + ", using name " + serviceName);
            }
        } else if (fs::is_directory(softwareTarPath)) {
            // 输入是已解压目录：解析服务名后移动到临时目录统一处理
            // 支持两种目录结构：dir/service.yaml 或 dir/<serviceName>/service.yaml
            std::string yamlPath = utils::JoinPath(softwareTarPath, DefaultServiceName);
            if (!fs::exists(yamlPath)) {
                // 尝试 dir/<serviceName>/service.yaml 结构
                yamlPath = utils::JoinPath(softwareTarPath, serviceName, DefaultServiceName);
            }
            auto configServiceName = ResolveServiceName(yamlPath);
            if (configServiceName.empty() || configServiceName != serviceName) {
                CleanupTempDirectory(extractDir);
                return MakeError("Failed to resolve service name from directory: " + softwareTarPath);
            }
            // 与 tar 分支保持一致：actualServiceName 用于后续 InstallSoftwarePackage
            actualServiceName = configServiceName;
            // 使用拷贝而非移动：避免安装失败时（服务被中断）用户源目录丢失无法恢复
            result = utils::CopyDirectory(softwareTarPath, extractDir);
            if (!result.IsDefalutSuccess()) {
                CleanupTempDirectory(extractDir);
                return result;
            }
            // 确保临时目录下存在 <serviceName>/ 子目录（与 tar 包解压后结构一致）
            std::string serviceSubDir = utils::JoinPath(extractDir, serviceName);
            if (!fs::exists(serviceSubDir)) {
                // service.yaml 直接在 extractDir 下：创建 serviceName 子目录并移入所有内容
                auto mkdirRet = utils::CreateDirectory(serviceSubDir);
                if (!mkdirRet.IsDefalutSuccess()) {
                    CleanupTempDirectory(extractDir);
                    return MakeError("Failed to create service subdirectory: " + mkdirRet.msg);
                }
                // 移动 extractDir 下所有条目到 serviceName 子目录（排除 serviceName 自身）
                for (auto &entry : fs::directory_iterator(extractDir)) {
                    if (entry.path().filename() == serviceName) {
                        continue;
                    }
                    std::string dst = utils::JoinPath(serviceSubDir, entry.path().filename().string());
                    fs::rename(entry.path(), dst);
                }
            } else {
                // 已存在 serviceName 子目录：仅重命名确保名称一致
                utils::RenameFirstSubdirectory(extractDir, serviceName);
            }
        } else {
            CleanupTempDirectory(extractDir);
            return MakeError("Invalid software path (not tar.gz or directory): " + softwareTarPath);
        }

        result = mFileManager->InstallSoftwarePackage(actualServiceName, extractDir);
        if (!result.IsDefalutSuccess()) {
            CleanupTempDirectory(extractDir);
            return MakeError("Failed to install software package: " + result.msg);
        }

        // 注册服务到 ConfigLoader（使用安装后的服务目录）
        std::string installedServiceDir = mFileManager->GetServiceWDir(actualServiceName);
        result = mConfigLoader->AddService(installedServiceDir);
        if (!result.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to register service in ConfigLoader: " << result.msg;
            // AddService 失败时需回滚：删除已拷贝的服务目录
            mFileManager->CleanupService(actualServiceName);
            CleanupTempDirectory(extractDir);
            return MakeError("Failed to register service in ConfigLoader: " + result.msg);
        }

        result = GenerateAndCreateServiceFile(actualServiceName);
        if (!result.IsDefalutSuccess()) {
            // 删除已经拷贝过来的服务目录
            mConfigLoader->RemoveService(actualServiceName);
            mFileManager->CleanupService(actualServiceName);

            CleanupTempDirectory(extractDir);
            return result;
        }

        CleanupTempDirectory(extractDir);

        // 为依赖模型文件的服务创建模型目录软链接（modelLinkDir 非空时创建）
        {
            auto* installedSvc = mConfigLoader->GetServiceByName(actualServiceName);
            if (installedSvc != nullptr && installedSvc->needModel && !installedSvc->modelLinkDir.empty()) {
                std::string linkPath = utils::JoinPath(installedSvc->currentServiceDir, installedSvc->modelLinkDir);
                std::string targetPath = mConfigLoader->GetConfigInfo().modelDir;
                SLOG_INFO << "Creating model symlink: " << linkPath << " -> " << targetPath;
                // 若已存在则先删除（可能是重复安装或旧残留）
                if (fs::exists(linkPath) || fs::is_symlink(linkPath)) {
                    fs::remove_all(linkPath);
                }
                // 确保父目录存在
                auto parentDir = fs::path(linkPath).parent_path().string();
                if (!parentDir.empty() && !fs::exists(parentDir)) {
                    auto mkdirRet = utils::CreateDirectory(parentDir);
                    if (!mkdirRet.IsDefalutSuccess()) {
                        SLOG_ERROR << "Failed to create parent directory for model symlink: " << parentDir;
                    }
                }
                auto symlinkResult = utils::CreateSymbolicLink(targetPath, linkPath);
                if (!symlinkResult.IsDefalutSuccess()) {
                    SLOG_ERROR << "Failed to create model symlink, rolling back installation: " << symlinkResult.msg;
                    mConfigLoader->RemoveService(actualServiceName);
                    mFileManager->CleanupService(actualServiceName);
                    return MakeError("Failed to create model symlink: " + symlinkResult.msg);
                }
                SLOG_INFO << "Model symlink created successfully: " << linkPath;
            }
        }

        SLOG_INFO << "Service installed successfully: " << actualServiceName;
        MarkSequenceDirty();

        // 安装后启动验证：若服务声明了 keep_alive_time_sec > 0，则启动并验证持续运行
        // 验证失败仅返回警告（code=1），不停止服务也不回滚安装（便于用户排查）
        // 注意：msg 始终为纯服务名，便于上层 service_ctl 继续后续处理（如数据库初始化）
        auto* installedSvc = mConfigLoader->GetServiceByName(actualServiceName);
        uint32_t keepSec = installedSvc ? installedSvc->keepAliveTimeSec : 0;
        if (keepSec > 0) {
            SLOG_INFO << "Verifying installed service keeps running for " << keepSec << "s: " << actualServiceName;
            auto startRet = StartService(actualServiceName);
            if (!startRet.IsDefalutSuccess()) {
                SLOG_WARN << "Installed service failed to start: " << startRet.msg;
                return MakeWarning(actualServiceName + " installed, but failed to start: " + startRet.msg);
            }
            std::this_thread::sleep_for(std::chrono::seconds(keepSec));
            if (!IsServiceActive(actualServiceName)) {
                SLOG_WARN << "Installed service is not active after " << keepSec << "s: " << actualServiceName;
                return MakeWarning(actualServiceName + " installed, but not active after " + std::to_string(keepSec) +
                                   "s verification");
            }
            SLOG_INFO << "Installed service verified running for " << keepSec << "s: " << actualServiceName;
        } else {
            SLOG_INFO << "Skip install verification (keep_alive_time_sec=0): " << actualServiceName;
        }

        return MakeResult(0, actualServiceName);
    }
    // NOLINTEND(readability-function-size, readability-function-cognitive-complexity)

    ResultMsg ServiceManager::StartService(const std::string &serviceName) {
        SLOG_INFO << "Starting service: " << serviceName;

        if (!mInitialized) {
            return MakeError("ServiceManager is not initialized");
        }

        auto* svc = mConfigLoader->GetServiceByName(serviceName);
        if (!svc) {
            return MakeError("Service not found: " + serviceName);
        }

        auto depResult = CheckServiceDependencies(serviceName);
        if (!depResult.IsDefalutSuccess()) {
            return depResult;
        }

        auto startDepResult = StartDependentServices(serviceName);
        if (!startDepResult.IsDefalutSuccess()) {
            return startDepResult;
        }

        // 使用 scmd_ 前缀的 systemd 单元名
        auto result = mDBusManager->StartUnit(ToSystemdUnitName(serviceName));
        if (!result.IsDefalutSuccess()) {
            return MakeError("Failed to start service: " + result.msg);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        auto stateResult = mDBusManager->GetUnitActiveState(ToSystemdUnitName(serviceName));
        // 接受 "active"（已运行）和 "activating"（启动中）两种状态为成功
        // 原因：systemd 启动某些服务（如 Type=simple 但依赖未就绪、或 Type=forking 派生中）
        //       会短暂停留在 activating，属正常过渡态，不应判定为失败
        if (stateResult.IsDefalutSuccess() && (stateResult.msg == "active" || stateResult.msg == "activating")) {
            SLOG_INFO << "Service started successfully: " << serviceName << " (state: " << stateResult.msg << ")";
            return MakeSuccess();
        } else {
            return MakeError("Service failed to start: unexpected state " + stateResult.msg);
        }
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg ServiceManager::StopService(const std::string &serviceName) {
        SLOG_INFO << "Stopping service: " << serviceName;

        if (!mInitialized) {
            return MakeError("ServiceManager is not initialized");
        }

        auto* svc = mConfigLoader->GetServiceByName(serviceName);
        if (!svc) {
            return MakeError("Service not found: " + serviceName);
        }

        // 检查反向依赖：是否有其他运行中的服务依赖此服务
        auto dependents = GetDependentServices(serviceName);
        for (const auto &depName : dependents) {
            auto depState = mDBusManager->GetUnitActiveState(ToSystemdUnitName(depName));
            if (depState.IsDefalutSuccess() && depState.msg == "active") {
                SLOG_WARN << "Warning: service " << depName << " depends on " << serviceName << " and is still running";
            }
        }

        auto result = mDBusManager->StopUnit(ToSystemdUnitName(serviceName));
        if (!result.IsDefalutSuccess()) {
            return MakeError("Failed to stop service: " + result.msg);
        }

        // 循环等待服务进入停止状态，超时时间使用配置中的 optTimeoutSec
        uint32_t timeoutSec = mConfigLoader->GetConfigInfo().optTimeoutSec;
        std::string finalState;
        for (uint32_t i = 0; i < timeoutSec * 2; ++i) {
            auto stateResult = mDBusManager->GetUnitActiveState(ToSystemdUnitName(serviceName));
            if (stateResult.IsDefalutSuccess()) {
                finalState = stateResult.msg;
                if (stateResult.msg == "inactive" || stateResult.msg == "failed") {
                    SLOG_INFO << "Service stopped successfully: " << serviceName;
                    return MakeSuccess();
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        // 超时未停止，直接 SIGKILL 兜底（优雅退出由 systemd 管理，此处仅负责强制终止）
        SLOG_WARN << "Service stop wait timeout, latest state: " << finalState
                  << ", force killing process for: " << serviceName;

        auto pidResult = mDBusManager->GetServiceMainPID(ToSystemdUnitName(serviceName));
        if (!pidResult.IsDefalutSuccess() || pidResult.msg.empty()) {
            SLOG_WARN << "Failed to get service PID, cannot kill: " << serviceName;
            return MakeError("Service stop timeout and unable to get PID: " + serviceName);
        }

        pid_t servicePid = 0;
        try {
            servicePid = static_cast<pid_t>(std::stoul(pidResult.msg));
        } catch (...) {
            SLOG_WARN << "Invalid PID value: " << pidResult.msg;
            return MakeError("Service stop timeout and invalid PID: " + pidResult.msg);
        }

        if (servicePid <= 0) {
            SLOG_WARN << "Service PID is 0 or invalid, process may have already exited: " << serviceName;
            return MakeError("Service stop timeout and PID is invalid: " + serviceName);
        }

        SLOG_INFO << "Sending SIGKILL to PID " << servicePid;
        if (kill(servicePid, SIGKILL) != 0) {
            SLOG_WARN << "Failed to send SIGKILL to PID " << servicePid << ": " << strerror(errno);
            return MakeError("Failed to kill service process: " + serviceName);
        }

        // 等待进程退出后确认最终状态
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        auto finalCheck = mDBusManager->GetUnitActiveState(ToSystemdUnitName(serviceName));
        if (finalCheck.IsDefalutSuccess() && (finalCheck.msg == "inactive" || finalCheck.msg == "failed")) {
            return MakeSuccess();
        }
        return MakeError("Service stop timeout and unexpected final state: " + finalCheck.msg);
    }

    ResultMsg ServiceManager::RestartService(const std::string &serviceName) {
        SLOG_INFO << "Restarting service: " << serviceName;

        if (!mInitialized) {
            return MakeError("ServiceManager is not initialized");
        }

        auto* svc = mConfigLoader->GetServiceByName(serviceName);
        if (!svc) {
            return MakeError("Service not found: " + serviceName);
        }

        // 使用 DBusManager::RestartUnit 原子操作，而非手动 stop+start
        auto result = mDBusManager->RestartUnit(ToSystemdUnitName(serviceName));
        if (!result.IsDefalutSuccess()) {
            return MakeError("Failed to restart service: " + result.msg);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        auto stateResult = mDBusManager->GetUnitActiveState(ToSystemdUnitName(serviceName));
        // 与 StartService 保持一致：接受 "active" 和 "activating" 为成功
        if (stateResult.IsDefalutSuccess() && (stateResult.msg == "active" || stateResult.msg == "activating")) {
            SLOG_INFO << "Service restarted successfully: " << serviceName << " (state: " << stateResult.msg << ")";
            return MakeSuccess();
        } else {
            return MakeError("Service failed to restart: unexpected state " + stateResult.msg);
        }
    }

    ResultMsg ServiceManager::StartScmdSelf() {
        // scmd 自身已经在运行中，直接返回成功
        SLOG_INFO << "Scmd self is already running, no action needed";
        return MakeSuccess();
    }

    ResultMsg ServiceManager::StopScmdSelf() {
        SLOG_INFO << "Stopping scmd self via systemd";
        return mDBusManager->StopUnit("qifeng-scmd.service");
    }

    ResultMsg ServiceManager::RestartScmdSelf() {
        SLOG_INFO << "Restarting scmd self via systemd";
        return mDBusManager->RestartUnit("qifeng-scmd.service");
    }

    // NOLINTNEXTLINE(readability-function-size,readability-function-cognitive-complexity)
    ResultMsg ServiceManager::ReloadService(const std::string &serviceName) {
        SLOG_INFO << "Reloading service: " << (serviceName.empty() ? "all" : serviceName);

        if (!mInitialized) {
            return MakeError("ServiceManager is not initialized");
        }

        if (serviceName.empty()) {
            // 重载所有服务：停止所有 -> 重新加载配置 -> 重新生成文件 -> 启动 auto-start
            auto result = StopAllServices();
            if (!result.IsDefalutSuccess()) {
                return MakeError("Failed to stop services before reload: " + result.msg);
            }

            auto allServices = mConfigLoader->GetAllServices();
            for (const auto &svc : allServices) {
                result = mConfigLoader->ReloadService(svc.serviceName);
                if (!result.IsDefalutSuccess()) {
                    SLOG_ERROR << "Failed to reload service config " << svc.serviceName << ": " << result.msg;
                    continue;
                }

                result = GenerateAndCreateServiceFile(svc.serviceName);
                if (!result.IsDefalutSuccess()) {
                    SLOG_ERROR << "Failed to regenerate service file for " << svc.serviceName << ": " << result.msg;
                }
            }
            MarkSequenceDirty();

            result = StartAllAutoStartServices();
            if (!result.IsDefalutSuccess()) {
                return MakeError("Failed to start services after reload: " + result.msg);
            }
            return MakeSuccess();
        }

        // 重载单个服务：停止 -> 重新加载配置 -> 重新生成文件 -> 启动
        auto result = StopService(serviceName);
        if (!result.IsDefalutSuccess()) {
            return MakeError("Failed to stop service before reload: " + result.msg);
        }

        result = mConfigLoader->ReloadService(serviceName);
        if (!result.IsDefalutSuccess()) {
            return result;
        }

        result = GenerateAndCreateServiceFile(serviceName);
        if (!result.IsDefalutSuccess()) {
            return result;
        }

        MarkSequenceDirty();

        result = StartService(serviceName);
        if (!result.IsDefalutSuccess()) {
            return MakeError("Failed to start service after reload: " + result.msg);
        }

        SLOG_INFO << "Service reloaded successfully: " << serviceName;
        return MakeSuccess();
    }

    ResultMsg ServiceManager::GetServiceStatus(const std::string &serviceName) {
        SLOG_INFO << "Getting status for service: " << serviceName;

        if (!mInitialized) {
            return MakeError("ServiceManager is not initialized");
        }

        auto* svc = mConfigLoader->GetServiceByName(serviceName);
        if (!svc) {
            return MakeError("Service not found: " + serviceName);
        }

        auto info = CollectRuntimeInfo(serviceName);

        // 使用 Json::Value 构建结构化数据，由 CLI 端统一格式化输出
        Json::Value root;
        root["serviceName"] = serviceName;
        root["version"] = info.currentVersion;
        root["pid"] = static_cast<int>(info.pid);
        root["status"] = info.status;
        root["startTime"] = info.startTime;
        root["runTime"] = info.runTime;
        root["memoryUsage"] = static_cast<Json::UInt64>(info.memoryUsage);
        root["cpuUsage"] = static_cast<Json::UInt64>(info.cpuUsage);
        root["configFilePath"] = info.configFilePath;
        root["rootPath"] = info.rootPath;
        root["dbFilePath"] = info.dbFilePath;
        root["recoveryCount"] = info.recoveryCount;

        Json::StreamWriterBuilder builder;
        builder["emitUTF8"] = true;
        std::string jsonStr = Json::writeString(builder, root);
        return ResultMsg {0, jsonStr};
    }

    ServiceRuntimeInfo ServiceManager::GetServiceRuntimeInfo(const std::string &serviceName) {
        return CollectRuntimeInfo(serviceName);
    }

    bool ServiceManager::IsServiceActive(const std::string &serviceName) {
        auto stateResult = mDBusManager->GetUnitActiveState(ToSystemdUnitName(serviceName));
        return stateResult.IsDefalutSuccess() && stateResult.msg == "active";
    }

    ResultMsg ServiceManager::UninstallService(const std::string &serviceName) {
        SLOG_INFO << "Uninstalling service: " << serviceName;

        if (!mInitialized) {
            return MakeError("ServiceManager is not initialized");
        }

        auto* svc = mConfigLoader->GetServiceByName(serviceName);
        if (!svc) {
            return MakeError("Service not found: " + serviceName);
        }

        // 先检查并停止运行中的服务
        auto stateResult = mDBusManager->GetUnitActiveState(ToSystemdUnitName(serviceName));
        if (stateResult.IsDefalutSuccess() && stateResult.msg == "active") {
            auto stopResult = StopService(serviceName);
            if (!stopResult.IsDefalutSuccess()) {
                SLOG_INFO << "Failed to stop service during uninstall: " << stopResult.msg;
            }
        }

        // 禁用开机自启
        auto disableResult = mDBusManager->DisableUnit(ToSystemdUnitName(serviceName));
        if (!disableResult.IsDefalutSuccess()) {
            SLOG_INFO << "Failed to disable auto-start during uninstall: " << disableResult.msg;
        }

        // RemoveSoftwarePackage 内部已包含 DeleteServiceFile 和 DeleteServiceSymlink
        mFileManager->CleanupService(serviceName);

        auto removeResult = mConfigLoader->RemoveService(serviceName);
        if (!removeResult.IsDefalutSuccess()) {
            return MakeError("Failed to remove service from ConfigLoader: " + removeResult.msg);
        }

        auto reloadResult = mDBusManager->ReloadDaemon();
        if (!reloadResult.IsDefalutSuccess()) {
            return MakeError("Failed to reload systemd daemon: " + reloadResult.msg);
        }

        SLOG_INFO << "Service uninstalled successfully: " << serviceName;
        MarkSequenceDirty();
        return MakeSuccess();
    }

    ResultMsg ServiceManager::ClearServiceData(const std::string &serviceName) {
        SLOG_INFO << "Clearing service data for " << serviceName;
        // 先检查并停止运行中的服务
        auto stateResult = mDBusManager->GetUnitActiveState(ToSystemdUnitName(serviceName));
        if (stateResult.IsDefalutSuccess() && stateResult.msg == "active") {
            auto stopResult = StopService(serviceName);
            if (!stopResult.IsDefalutSuccess()) {
                SLOG_INFO << "Failed to stop service during uninstall: " << stopResult.msg;
            }
        }

        // 禁用开机自启
        auto disableResult = mDBusManager->DisableUnit(ToSystemdUnitName(serviceName));
        if (!disableResult.IsDefalutSuccess()) {
            SLOG_INFO << "Failed to disable auto-start during uninstall: " << disableResult.msg;
        }

        mFileManager->CleanupService(serviceName);
        mConfigLoader->RemoveService(serviceName);
        mDBusManager->ReloadDaemon();

        return MakeSuccess();
    }

    // NOLINTBEGIN(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg ServiceManager::UpdateService(const std::string &serviceName, const std::string &softwareTarPath) {
        SLOG_INFO << "Updating service: " << serviceName << " with: " << softwareTarPath;

        if (!mInitialized) {
            return MakeError("ServiceManager is not initialized");
        }

        auto* svc = mConfigLoader->GetServiceByName(serviceName);
        if (!svc) {
            return MakeError("Service not found: " + serviceName);
        }

        // 记录服务是否正在运行，更新后恢复
        auto stateResult = mDBusManager->GetUnitActiveState(ToSystemdUnitName(serviceName));
        bool wasRunning = (stateResult.IsDefalutSuccess() && stateResult.msg == "active");

        if (wasRunning) {
            auto stopResult = StopService(serviceName);
            if (!stopResult.IsDefalutSuccess()) {
                return MakeError("Failed to stop service for update: " + stopResult.msg);
            }
        }

        std::string tempDir = utils::GenerateTempDir(mFileManager->GetCurDirConfig().tempDir);
        auto result = PrepareUpgradeSource(serviceName, softwareTarPath, tempDir);
        if (!result.IsDefalutSuccess()) {
            CleanupTempDirectory(tempDir);
            return result;
        }

        // 版本号递增校验：新版本必须严格大于当前已安装版本，阻止降级和平级覆盖
        std::string sourceDir = utils::JoinPath(tempDir, serviceName);
        std::string newYamlPath = utils::JoinPath(sourceDir, DefaultServiceName);
        std::string newVersion = utils::ReadVersion(newYamlPath);
        if (newVersion.empty()) {
            CleanupTempDirectory(tempDir);
            return MakeError("Failed to read version from new package: " + newYamlPath);
        }
        if (utils::CompareVersion(newVersion, svc->version) <= 0) {
            CleanupTempDirectory(tempDir);
            return MakeError("Version must be greater than current (" + svc->version + "), got: " + newVersion);
        }
        SLOG_INFO << "Version check passed: " << svc->version << " -> " << newVersion;

        // 探测新版本包是否含 up_detail.yaml：存在则细粒度升级，否则默认全量升级
        // sourceDir 指向服务内容目录（tempDir/serviceName），供 UpdateServiceWithDetail 使用
        // tempDir 为父目录，供 UpdateServiceDefault 使用（内部 UpgradeSoftwarePackage 会拼接 serviceName）
        std::string upDetailPath = utils::JoinPath(sourceDir, "up_detail.yaml");
        if (fs::exists(upDetailPath)) {
            result = UpdateServiceWithDetail(serviceName, sourceDir, upDetailPath, wasRunning);
        } else {
            result = UpdateServiceDefault(serviceName, tempDir, wasRunning);
        }

        CleanupTempDirectory(tempDir);
        if (result.IsDefalutSuccess()) {
            SLOG_INFO << "Service updated successfully: " << serviceName;
            MarkSequenceDirty();
        }
        return result;
    }
    // NOLINTEND(readability-function-size, readability-function-cognitive-complexity)

    ResultMsg ServiceManager::CleanUpgradeBackup(const std::string &serviceName) {
        return mFileManager->CleanBackup(serviceName);
    }

    ResultMsg ServiceManager::VerifyAndRestoreServiceState(const std::string &serviceName, bool wasRunning,
                                                           bool useFineGrained) {
        // 读取升级后服务配置中的 keep_alive_time_sec
        auto* upgradedSvc = mConfigLoader->GetServiceByName(serviceName);
        uint32_t keepSec = upgradedSvc ? upgradedSvc->keepAliveTimeSec : 0;
        if (keepSec == 0) {
            // 未配置验证时长：仅按升级前状态恢复（原未运行则保持停止，原运行则保持运行）
            if (wasRunning) {
                auto startRet = StartService(serviceName);
                if (!startRet.IsDefalutSuccess()) {
                    SLOG_ERROR << "Failed to start service after upgrade: " << startRet.msg;
                    DoRollbackUpgrade(serviceName, wasRunning, useFineGrained);
                    return startRet;
                }
            }
            return MakeSuccess();
        }

        // keep_alive_time_sec > 0：无论升级前是否运行，都启动并验证
        SLOG_INFO << "Starting service for keep_alive verification: " << serviceName << " (" << keepSec << "s)";
        auto startRet = StartService(serviceName);
        if (!startRet.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to start service after upgrade: " << startRet.msg;
            DoRollbackUpgrade(serviceName, wasRunning, useFineGrained);
            return startRet;
        }

        // 等待验证时长，检查服务是否持续运行
        std::this_thread::sleep_for(std::chrono::seconds(keepSec));
        if (!IsServiceActive(serviceName)) {
            SLOG_ERROR << "Service not active after " << keepSec << "s verification";
            DoRollbackUpgrade(serviceName, wasRunning, useFineGrained);
            return MakeError("Service not active after " + std::to_string(keepSec) + "s verification");
        }
        SLOG_INFO << "Service kept alive for " << keepSec << "s: " << serviceName;

        // 验证通过：若升级前未运行，则停止服务恢复原状态
        if (!wasRunning) {
            auto stopRet = StopService(serviceName);
            if (!stopRet.IsDefalutSuccess()) {
                SLOG_WARN << "Failed to restore stopped state after verification: " << stopRet.msg;
                // 停止失败不视为升级失败，仅告警（服务已验证可用）
            }
        }
        return MakeSuccess();
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    void ServiceManager::DoRollbackUpgrade(const std::string &serviceName, bool restartIfWasRunning,
                                           bool useFineGrained) {
        SLOG_INFO << "Rolling back upgrade for service: " << serviceName << " (fineGrained=" << useFineGrained << ")";

        // 1. 回滚文件到旧版本
        ResultMsg rollbackResult;
        if (useFineGrained) {
            rollbackResult = mFileManager->RollbackFineGrainedUpgrade(serviceName);
        } else {
            rollbackResult = mFileManager->RollbackSoftwarePackage(serviceName);
        }
        if (!rollbackResult.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to rollback software package: " << rollbackResult.msg;
        }

        // 2. 重新加载配置以匹配回滚后的文件
        auto reloadResult = mConfigLoader->ReloadService(serviceName);
        if (!reloadResult.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to reload service config after rollback: " << reloadResult.msg;
        }

        // 3. 重新生成 systemd 服务文件
        auto genResult = GenerateAndCreateServiceFile(serviceName);
        if (!genResult.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to regenerate service file after rollback: " << genResult.msg;
        }

        // 4. 回滚数据库（若 db_backup 存在）
        auto dbRollback = RollbackDatabase(serviceName);
        if (!dbRollback.IsDefalutSuccess()) {
            SLOG_WARN << "Database rollback skipped or failed: " << dbRollback.msg;
        }

        // 5. 如果升级前服务在运行，尝试重新启动
        if (restartIfWasRunning) {
            auto startResult = StartService(serviceName);
            if (!startResult.IsDefalutSuccess()) {
                SLOG_ERROR << "Failed to restart service after rollback: " << startResult.msg;
            }
        }

        // 6. 回滚完成后清理备份
        auto cleanResult = mFileManager->CleanBackup(serviceName);
        if (!cleanResult.IsDefalutSuccess()) {
            SLOG_WARN << "Failed to clean backup after rollback: " << cleanResult.msg;
        }

        SLOG_INFO << "Upgrade rollback completed for service: " << serviceName;
    }

    // --- 独立 nginx 配置管理 ---

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg ServiceManager::InitNginx(const std::string &dirPath) {
        SLOG_INFO << "InitNginx from: " << dirPath;

        if (!mInitialized) {
            return MakeError("ServiceManager is not initialized");
        }

        // 解析源路径：tar 包则解压，目录则直接使用
        std::string nginxSrcDir;
        std::string tempDir;

        if (IsTarPackage(dirPath)) {
            // tar 包：解压到临时目录（不指定子目录名，由 FileManager 内部查找 nginx/frontend 子目录）
            tempDir = utils::GenerateTempDir(mFileManager->GetCurDirConfig().tempDir);
            auto extractResult = ExtractSoftwareTar(dirPath, tempDir);
            if (!extractResult.IsDefalutSuccess()) {
                CleanupTempDirectory(tempDir);
                return MakeError("Failed to extract tar for nginx: " + extractResult.msg);
            }
            // 直接使用解压目录，FileManager::InitNginx 会查找 nginx/frontend 子目录
            nginxSrcDir = tempDir;
        } else if (fs::is_directory(dirPath)) {
            nginxSrcDir = dirPath;
        } else {
            return MakeError("Invalid path (not tar.gz or directory): " + dirPath);
        }

        // 调用 FileManager 安装 nginx 配置（含备份回退机制）
        auto result = mFileManager->InitNginx(nginxSrcDir);

        // 清理临时目录
        if (!tempDir.empty()) {
            CleanupTempDirectory(tempDir);
        }

        if (!result.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to init nginx: " << result.msg;
            return result;
        }

        // 测试并启动/重载系统 nginx
        tool::Nginx nginx;
        if (!nginx.IsInstalled()) {
            SLOG_WARN << "nginx is not installed on system, skip start";
            return MakeSuccess();
        }

        // 1. 先检查系统默认配置是否有效
        auto testResult = nginx.TestSystemConfig();
        if (!testResult.IsDefalutSuccess()) {
            // 配置测试失败，回退 nginx 配置
            SLOG_ERROR << "nginx system config test failed, rolling back: " << testResult.msg;
            auto rollbackResult = mFileManager->ResetNginx();
            if (!rollbackResult.IsDefalutSuccess()) {
                SLOG_WARN << "Failed to rollback nginx config: " << rollbackResult.msg;
            }
            return MakeError("nginx config test failed: " + testResult.msg);
        }

        // 2. 配置有效后，运行中则 reload，未运行则启动
        ResultMsg applyResult;
        if (nginx.IsRunning()) {
            applyResult = nginx.Reload();
            if (!applyResult.IsDefalutSuccess()) {
                SLOG_ERROR << "nginx reload failed: " << applyResult.msg;
                return applyResult;
            }
            SLOG_INFO << "nginx reloaded after config test";
        } else {
            applyResult = nginx.StartSystem();
            if (!applyResult.IsDefalutSuccess()) {
                SLOG_ERROR << "nginx system start failed: " << applyResult.msg;
                return applyResult;
            }
            SLOG_INFO << "nginx started with system default config";
        }

        SLOG_INFO << "Nginx initialized successfully";
        return MakeSuccess();
    }

    ResultMsg ServiceManager::ResetNginx(NginxResetMode mode) {
        SLOG_INFO << "ResetNginx mode=" << static_cast<int>(mode);

        if (!mInitialized) {
            return MakeError("ServiceManager is not initialized");
        }

        // 根据 mode 分发到不同的 FileManager 操作
        ResultMsg result;
        switch (mode) {
            case NginxResetMode::WAIT:
                result = mFileManager->SetNginxWaiting();
                break;
            case NginxResetMode::NORMAL:
                result = mFileManager->SetNginxNormal();
                break;
            case NginxResetMode::BACK:
                result = mFileManager->ResetNginx();
                break;
            default:
                return MakeError("Invalid nginx reset mode");
        }

        if (!result.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to reset nginx: " << result.msg;
            return result;
        }

        // reload 系统 nginx 使变更生效
        tool::Nginx nginx;
        if (nginx.IsInstalled() && nginx.IsRunning()) {
            auto reloadResult = nginx.Reload();
            if (!reloadResult.IsDefalutSuccess()) {
                SLOG_WARN << "Failed to reload nginx after reset: " << reloadResult.msg;
            }
        }

        SLOG_INFO << "Nginx reset successfully";
        return MakeSuccess();
    }

    // --- 模型管理 ---

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg ServiceManager::AddModel(const std::string &srcPath) {
        // 1. 前置校验
        if (!mInitialized) {
            return MakeError("ServiceManager is not initialized");
        }
        if (srcPath.empty()) {
            return MakeError("Model source path is empty");
        }
        const auto &configInfo = mConfigLoader->GetConfigInfo();
        if (configInfo.modelDir.empty()) {
            return MakeError("model_dir is not configured in scmd.yaml");
        }
        if (!fs::exists(srcPath)) {
            return MakeError("Model source path does not exist: " + srcPath);
        }

        // 确保模型目录存在（惰性创建，与 nginx 目录处理风格一致）
        auto mkdirRet = utils::CreateDirectory(configInfo.modelDir);
        if (!mkdirRet.IsDefalutSuccess()) {
            return MakeError("Failed to create model_dir: " + mkdirRet.msg);
        }

        // 2. 解析模型名并准备源目录
        //    - tar/tar.gz：解压到临时目录，取解压后唯一顶层目录名为模型名
        //    - 目录：basename 为模型名
        std::string modelName;
        std::string preparedSrcDir;  // 最终用于移动到 modelDir 的源目录
        std::string tempDir;         // tar 解压临时目录（非空时需要清理）
        if (IsTarPackage(srcPath)) {
            tempDir = utils::GenerateTempDir(mFileManager->GetCurDirConfig().tempDir);
            auto extractRet = ExtractSoftwareTar(srcPath, tempDir);
            if (!extractRet.IsDefalutSuccess()) {
                CleanupTempDirectory(tempDir);
                return MakeError("Failed to extract model tar: " + extractRet.msg);
            }
            // 取解压目录下唯一的顶层条目名（要求是目录）
            modelName = utils::GetSingleTopLevelEntryName(tempDir);
            if (modelName.empty()) {
                CleanupTempDirectory(tempDir);
                return MakeError("Model tar must contain exactly one top-level directory");
            }
            preparedSrcDir = utils::JoinPath(tempDir, modelName);
        } else if (fs::is_directory(srcPath)) {
            modelName = fs::path(srcPath).filename().string();
            preparedSrcDir = srcPath;
        } else {
            return MakeError("Invalid model path (not tar.gz or directory): " + srcPath);
        }

        auto nameRet = ValidateModelName(modelName);
        if (!nameRet.IsDefalutSuccess()) {
            CleanupTempDirectory(tempDir);
            return nameRet;
        }

        std::string modelDir = configInfo.modelDir;
        std::string dstPath = utils::JoinPath(modelDir, modelName);
        std::string backupPath = utils::JoinPath(modelDir, modelName + ".back");

        // 3. 停止依赖模型的服务，记录停止前状态
        auto dependentServices = GetModelDependentServices();
        std::map<std::string, bool> preStates;
        auto stopRet = StopServicesWithStateRecord(dependentServices, preStates);
        if (!stopRet.IsDefalutSuccess()) {
            // 停止失败直接返回，不修改模型文件
            CleanupTempDirectory(tempDir);
            return MakeError("Failed to stop model-dependent services: " + stopRet.msg);
        }

        // 4. 备份原模型（若存在 .back 先删除）
        bool hadExistingModel = fs::exists(dstPath);
        if (hadExistingModel) {
            if (fs::exists(backupPath)) {
                utils::ForceDeleteDirectory(backupPath);
            }
            std::error_code ec;
            fs::rename(dstPath, backupPath, ec);
            if (ec) {
                RestoreServicesByState(preStates);
                CleanupTempDirectory(tempDir);
                return MakeError("Failed to backup existing model: " + ec.message());
            }
        }

        // 5. 复制新模型到 model_dir/<name>（使用拷贝而非移动，避免验证失败回退时用户源目录丢失）
        auto moveRet = utils::CopyDirectory(preparedSrcDir, dstPath);
        if (!moveRet.IsDefalutSuccess()) {
            // 回退：恢复备份
            if (hadExistingModel) {
                std::error_code ec;
                fs::rename(backupPath, dstPath, ec);
            }
            RestoreServicesByState(preStates);
            CleanupTempDirectory(tempDir);
            return MakeError("Failed to copy model to model_dir: " + moveRet.msg);
        }
        CleanupTempDirectory(tempDir);

        // 6. 启动依赖服务并按各自 keep_alive_time_sec 验证持续运行
        auto verifyRet = StartServicesAndWaitRunning(dependentServices);
        if (!verifyRet.IsDefalutSuccess()) {
            SLOG_ERROR << "Model verification failed: " << verifyRet.msg << ", rolling back model";
            // 回退模型：删除新模型，恢复备份
            utils::ForceDeleteDirectory(dstPath);
            if (hadExistingModel) {
                std::error_code ec;
                fs::rename(backupPath, dstPath, ec);
            }
            // 恢复服务起初状态
            RestoreServicesByState(preStates);
            return MakeError("Model verification failed, rolled back: " + verifyRet.msg);
        }

        // 7. 验证通过，恢复服务起初状态（原本运行的保持运行，原本停止的停止）
        RestoreServicesByState(preStates);

        // 8. 清理备份（add_model 成功后不再保留 .back）
        if (hadExistingModel && fs::exists(backupPath)) {
            utils::ForceDeleteDirectory(backupPath);
        }

        SLOG_INFO << "Model added successfully: " << modelName;
        return MakeSuccess();
    }

    std::vector<std::string> ServiceManager::GetModelDependentServices(const std::string &excludeService) {
        std::vector<std::string> result;
        auto allServices = mConfigLoader->GetAllServices();
        for (const auto &svc : allServices) {
            if (svc.needModel && svc.serviceName != excludeService) {
                result.push_back(svc.serviceName);
            }
        }
        return result;
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg ServiceManager::AddModelWithBackupRetained(const std::string &srcPath, const std::string &excludeService,
                                                         std::string &modelName) {
        // 与 AddModel 流程一致，但：1) 排除 excludeService  2) 不清理 .back 备份
        if (!mInitialized) {
            return MakeError("ServiceManager is not initialized");
        }
        if (srcPath.empty()) {
            return MakeError("Model source path is empty");
        }
        const auto &configInfo = mConfigLoader->GetConfigInfo();
        if (configInfo.modelDir.empty()) {
            return MakeError("model_dir is not configured in scmd.yaml");
        }
        if (!fs::exists(srcPath)) {
            return MakeError("Model source path does not exist: " + srcPath);
        }

        auto mkdirRet = utils::CreateDirectory(configInfo.modelDir);
        if (!mkdirRet.IsDefalutSuccess()) {
            return MakeError("Failed to create model_dir: " + mkdirRet.msg);
        }

        // 解析模型名并准备源目录
        std::string preparedSrcDir;
        std::string tempDir;
        if (IsTarPackage(srcPath)) {
            tempDir = utils::GenerateTempDir(mFileManager->GetCurDirConfig().tempDir);
            auto extractRet = ExtractSoftwareTar(srcPath, tempDir);
            if (!extractRet.IsDefalutSuccess()) {
                CleanupTempDirectory(tempDir);
                return MakeError("Failed to extract model tar: " + extractRet.msg);
            }
            modelName = utils::GetSingleTopLevelEntryName(tempDir);
            if (modelName.empty()) {
                CleanupTempDirectory(tempDir);
                return MakeError("Model tar must contain exactly one top-level directory");
            }
            preparedSrcDir = utils::JoinPath(tempDir, modelName);
        } else if (fs::is_directory(srcPath)) {
            modelName = fs::path(srcPath).filename().string();
            preparedSrcDir = srcPath;
        } else {
            return MakeError("Invalid model path (not tar.gz or directory): " + srcPath);
        }

        auto nameRet = ValidateModelName(modelName);
        if (!nameRet.IsDefalutSuccess()) {
            CleanupTempDirectory(tempDir);
            return nameRet;
        }

        std::string modelDir = configInfo.modelDir;
        std::string dstPath = utils::JoinPath(modelDir, modelName);
        std::string backupPath = utils::JoinPath(modelDir, modelName + ".back");

        // 停止依赖模型的服务（排除当前升级服务）
        auto dependentServices = GetModelDependentServices(excludeService);
        std::map<std::string, bool> preStates;
        auto stopRet = StopServicesWithStateRecord(dependentServices, preStates);
        if (!stopRet.IsDefalutSuccess()) {
            CleanupTempDirectory(tempDir);
            return MakeError("Failed to stop model-dependent services: " + stopRet.msg);
        }

        // 备份原模型（若存在 .back 先删除）
        bool hadExistingModel = fs::exists(dstPath);
        if (hadExistingModel) {
            if (fs::exists(backupPath)) {
                utils::ForceDeleteDirectory(backupPath);
            }
            std::error_code ec;
            fs::rename(dstPath, backupPath, ec);
            if (ec) {
                RestoreServicesByState(preStates);
                CleanupTempDirectory(tempDir);
                return MakeError("Failed to backup existing model: " + ec.message());
            }
        }

        // 复制新模型到 model_dir/<name>（使用拷贝而非移动，避免验证失败回退时用户源目录丢失）
        auto moveRet = utils::CopyDirectory(preparedSrcDir, dstPath);
        if (!moveRet.IsDefalutSuccess()) {
            if (hadExistingModel) {
                std::error_code ec;
                fs::rename(backupPath, dstPath, ec);
            }
            RestoreServicesByState(preStates);
            CleanupTempDirectory(tempDir);
            return MakeError("Failed to copy model to model_dir: " + moveRet.msg);
        }
        CleanupTempDirectory(tempDir);

        // 启动依赖服务并验证
        auto verifyRet = StartServicesAndWaitRunning(dependentServices);
        if (!verifyRet.IsDefalutSuccess()) {
            SLOG_ERROR << "Model verification failed: " << verifyRet.msg << ", rolling back model";
            utils::ForceDeleteDirectory(dstPath);
            if (hadExistingModel) {
                std::error_code ec;
                fs::rename(backupPath, dstPath, ec);
            }
            RestoreServicesByState(preStates);
            return MakeError("Model verification failed, rolled back: " + verifyRet.msg);
        }

        // 验证通过，恢复服务起初状态（注意：不清理 .back 备份，由调用方负责）
        RestoreServicesByState(preStates);

        SLOG_INFO << "Model added (backup retained): " << modelName;
        return MakeSuccess();
    }

    ResultMsg ServiceManager::CleanModelBackup(const std::string &modelName) {
        if (!mInitialized) {
            return MakeError("ServiceManager is not initialized");
        }
        if (modelName.empty()) {
            return MakeError("Model name is empty");
        }
        const auto &configInfo = mConfigLoader->GetConfigInfo();
        std::string backupPath = utils::JoinPath(configInfo.modelDir, modelName + ".back");
        if (!fs::exists(backupPath)) {
            SLOG_INFO << "No backup to clean for model: " << modelName;
            return MakeSuccess();
        }
        auto ret = utils::ForceDeleteDirectory(backupPath);
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Failed to clean model backup: " + ret.msg);
        }
        SLOG_INFO << "Model backup cleaned: " << modelName;
        return MakeSuccess();
    }

    ResultMsg ServiceManager::RollbackModel(const std::string &modelName) {
        if (!mInitialized) {
            return MakeError("ServiceManager is not initialized");
        }
        if (modelName.empty()) {
            return MakeError("Model name is empty");
        }
        const auto &configInfo = mConfigLoader->GetConfigInfo();
        std::string dstPath = utils::JoinPath(configInfo.modelDir, modelName);
        std::string backupPath = utils::JoinPath(configInfo.modelDir, modelName + ".back");

        // 删除新安装的模型
        if (fs::exists(dstPath)) {
            auto delRet = utils::ForceDeleteDirectory(dstPath);
            if (!delRet.IsDefalutSuccess()) {
                SLOG_ERROR << "Failed to delete new model: " << delRet.msg;
            }
        }
        // 恢复 .back 备份
        if (fs::exists(backupPath)) {
            std::error_code ec;
            fs::rename(backupPath, dstPath, ec);
            if (ec) {
                return MakeError("Failed to restore model backup: " + ec.message());
            }
            SLOG_INFO << "Model rolled back: " << modelName;
        } else {
            SLOG_INFO << "Model rolled back (no backup existed): " << modelName;
        }
        return MakeSuccess();
    }

    /**
     * @brief 判断 name 是否以 serviceName 为前缀，兼容 '-' 与 '_' 互换的命名
     * @details 安装包/升级素材目录可能使用连字符（如 qifeng-ca），而服务名使用下划线（如 qifeng_ca），
     *          两者在语义上代表同一服务，因此前缀匹配时将 '-' 和 '_' 视为等价。
     * @param name 待匹配的目录/文件名
     * @param serviceName 服务名
     * @return bool true 表示 name 以 serviceName（兼容 -/_）为前缀
     */
    static bool StartsWithServiceName(const std::string &name, const std::string &serviceName) {
        if (name.size() < serviceName.size()) {
            return false;
        }
        for (size_t i = 0; i < serviceName.size(); ++i) {
            char n = name[i];
            char s = serviceName[i];
            if (n == s) {
                continue;
            }
            // 将 '-' 和 '_' 视为等价字符
            if ((n == '-' && s == '_') || (n == '_' && s == '-')) {
                continue;
            }
            return false;
        }
        return true;
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg ServiceManager::FindUpgradeArtifacts(const std::string &serviceName, const std::string &srcPath,
                                                   UpgradeArtifacts &artifacts) {
        artifacts = UpgradeArtifacts {};

        // 确定扫描目录：srcPath 为空时用服务内部 soft_dir；tar 包时先解压
        std::string scanDir;
        std::string tempDir;

        if (srcPath.empty()) {
            // 从服务内部 soft_dir 扫描
            auto* svc = mConfigLoader->GetServiceByName(serviceName);
            if (svc == nullptr) {
                return MakeError("Service not found: " + serviceName);
            }
            if (svc->upgradeConfig.softDir.empty()) {
                return MakeError("Service " + serviceName + " has no upgrade.soft_dir configured");
            }
            scanDir = utils::GetAbsolutePath(utils::JoinPath(svc->currentServiceDir, svc->upgradeConfig.softDir));
            SLOG_INFO << "Scanning internal soft_dir: " << scanDir;
        } else if (IsTarPackage(srcPath)) {
            // tar 包：解压到临时目录
            tempDir = utils::GenerateTempDir(mFileManager->GetCurDirConfig().tempDir);
            auto extractRet = ExtractSoftwareTar(srcPath, tempDir);
            if (!extractRet.IsDefalutSuccess()) {
                CleanupTempDirectory(tempDir);
                return MakeError("Failed to extract artifacts tar: " + extractRet.msg);
            }
            scanDir = tempDir;
            artifacts.tempDir = tempDir;
            SLOG_INFO << "Scanning extracted tar dir: " << scanDir;
        } else if (fs::is_directory(srcPath)) {
            scanDir = srcPath;
            SLOG_INFO << "Scanning dir: " << scanDir;
        } else {
            return MakeError("Invalid srcPath (not tar.gz or directory): " + srcPath);
        }

        if (!fs::exists(scanDir)) {
            return MakeError("Scan directory does not exist: " + scanDir);
        }

        // 扫描目录下的所有条目，按前缀规则分类
        std::vector<std::string> servicePkgs;
        std::vector<std::string> modelEntries;
        std::vector<std::string> nginxDirs;

        std::error_code ec;
        for (auto &entry : fs::directory_iterator(scanDir, ec)) {
            if (ec) {
                // 迭代过程中出错，记录并停止扫描
                SLOG_WARN << "Error iterating directory: " << ec.message();
                break;
            }
            std::string name = entry.path().filename().string();
            // 模型素材：model* 前缀的目录或 tar 包（优先匹配，避免与服务包前缀冲突）
            if (name.find("model") == 0) {
                if (fs::is_directory(entry.path()) || IsTarPackage(name)) {
                    modelEntries.push_back(entry.path().string());
                }
                continue;
            }
            // nginx 素材：nginx* 前缀的目录
            if (name.find("nginx") == 0 && fs::is_directory(entry.path())) {
                nginxDirs.push_back(entry.path().string());
                continue;
            }
            // 服务包：<serviceName>*.tar.gz 或 <serviceName>* 前缀的目录（与 install/upgrade 的 -d 行为一致）
            // 目录/文件名中的 '-' 与 '_' 视为等价，兼容 qifeng-ca 与 qifeng_ca 两种命名风格
            if (StartsWithServiceName(name, serviceName)) {
                if (utils::HasSuffix(name, ".tar.gz") || fs::is_directory(entry.path())) {
                    servicePkgs.push_back(entry.path().string());
                }
                continue;
            }
        }

        // 排序后取第一个，保证确定性
        std::sort(servicePkgs.begin(), servicePkgs.end());
        std::sort(modelEntries.begin(), modelEntries.end());
        std::sort(nginxDirs.begin(), nginxDirs.end());

        if (!servicePkgs.empty()) {
            artifacts.servicePackage = servicePkgs[0];
            SLOG_INFO << "Found service package: " << artifacts.servicePackage;
        }
        if (!modelEntries.empty()) {
            artifacts.modelPath = modelEntries[0];
            SLOG_INFO << "Found model artifact: " << artifacts.modelPath;
        }
        if (!nginxDirs.empty()) {
            artifacts.nginxDir = nginxDirs[0];
            SLOG_INFO << "Found nginx artifact: " << artifacts.nginxDir;
        }

        // 至少需要一个素材
        if (artifacts.servicePackage.empty() && artifacts.modelPath.empty() && artifacts.nginxDir.empty()) {
            if (!tempDir.empty()) {
                CleanupTempDirectory(tempDir);
                artifacts.tempDir.clear();
            }
            return MakeError("No upgrade artifacts found in: " + scanDir);
        }

        return MakeSuccess();
    }

    void ServiceManager::CleanupArtifactsTempDir(UpgradeArtifacts &artifacts) {
        if (!artifacts.tempDir.empty()) {
            CleanupTempDirectory(artifacts.tempDir);
            artifacts.tempDir.clear();
        }
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg ServiceManager::ClearModel(const std::string &modelName) {
        // 1. 前置校验
        if (!mInitialized) {
            return MakeError("ServiceManager is not initialized");
        }
        auto nameRet = ValidateModelName(modelName);
        if (!nameRet.IsDefalutSuccess()) {
            return nameRet;
        }
        const auto &configInfo = mConfigLoader->GetConfigInfo();
        if (configInfo.modelDir.empty()) {
            return MakeError("model_dir is not configured in scmd.yaml");
        }

        std::string modelDir = configInfo.modelDir;
        std::string modelPath = utils::JoinPath(modelDir, modelName);
        std::string backupPath = utils::JoinPath(modelDir, modelName + ".back");

        if (!fs::exists(modelPath)) {
            return MakeError("Model does not exist: " + modelPath);
        }

        // 2. 停止依赖模型的服务
        auto dependentServices = GetModelDependentServices();
        std::map<std::string, bool> preStates;
        auto stopRet = StopServicesWithStateRecord(dependentServices, preStates);
        if (!stopRet.IsDefalutSuccess()) {
            return MakeError("Failed to stop model-dependent services: " + stopRet.msg);
        }

        // 3. 重命名模型为 <name>.back（若已存在先删除）
        if (fs::exists(backupPath)) {
            utils::ForceDeleteDirectory(backupPath);
        }
        std::error_code ec;
        fs::rename(modelPath, backupPath, ec);
        if (ec) {
            RestoreServicesByState(preStates);
            return MakeError("Failed to rename model to .back: " + ec.message());
        }

        // 4. 启动依赖服务并按各自 keep_alive_time_sec 验证持续运行
        auto verifyRet = StartServicesAndWaitRunning(dependentServices);
        if (!verifyRet.IsDefalutSuccess()) {
            SLOG_ERROR << "Model clear verification failed: " << verifyRet.msg << ", rolling back";
            // 回退：恢复模型名
            std::error_code renameEc;
            fs::rename(backupPath, modelPath, renameEc);
            RestoreServicesByState(preStates);
            return MakeError("Model clear verification failed, rolled back: " + verifyRet.msg);
        }

        // 5. 验证通过，恢复服务起初状态
        RestoreServicesByState(preStates);

        // 6. 清理 .back 备份（clear_model 成功后不再保留 .back，与 add_model 行为对称）
        if (fs::exists(backupPath)) {
            utils::ForceDeleteDirectory(backupPath);
        }

        SLOG_INFO << "Model cleared successfully: " << modelName;
        return MakeSuccess();
    }

    // --- 模型管理辅助 ---

    std::vector<std::string> ServiceManager::GetModelDependentServices() {
        std::vector<std::string> result;
        auto allServices = mConfigLoader->GetAllServices();
        for (const auto &svc : allServices) {
            if (svc.needModel) {
                result.push_back(svc.serviceName);
            }
        }
        return result;
    }

    ResultMsg ServiceManager::StopServicesWithStateRecord(const std::vector<std::string> &serviceNames,
                                                          std::map<std::string, bool> &preStates) {
        preStates.clear();
        for (const auto &name : serviceNames) {
            // 记录停止前是否在运行
            bool wasRunning = IsServiceActive(name);
            preStates[name] = wasRunning;
            if (wasRunning) {
                auto stopRet = StopService(name);
                if (!stopRet.IsDefalutSuccess()) {
                    // 停止失败：已停止的服务保持原状态记录，直接返回错误
                    return MakeError("Failed to stop service " + name + ": " + stopRet.msg);
                }
            }
        }
        return MakeSuccess();
    }

    ResultMsg ServiceManager::RestoreServicesByState(const std::map<std::string, bool> &preStates) {
        // 按停止前状态恢复：原本运行的启动，原本停止的保持停止
        // 失败仅告警不中断，确保回退流程能继续执行
        for (const auto &kv : preStates) {
            if (kv.second) {
                // 原本在运行，尝试启动
                auto startRet = StartService(kv.first);
                if (!startRet.IsDefalutSuccess()) {
                    SLOG_WARN << "Failed to restore service " << kv.first << " to running state: " << startRet.msg;
                }
            }
        }
        return MakeSuccess();
    }

    ResultMsg ServiceManager::StartServicesAndWaitRunning(const std::vector<std::string> &serviceNames) {
        // 逐个启动服务并按各自的 keep_alive_time_sec 验证持续运行
        // 每个服务独立验证：启动后等待 keepAliveTimeSec 秒，再检查是否仍活跃
        for (const auto &name : serviceNames) {
            auto startRet = StartService(name);
            if (!startRet.IsDefalutSuccess()) {
                return MakeError("Failed to start service " + name + ": " + startRet.msg);
            }

            // 查询服务的 keep_alive_time_sec：0 表示跳过验证
            auto* svc = mConfigLoader->GetServiceByName(name);
            uint32_t keepSec = svc ? svc->keepAliveTimeSec : 0;
            if (keepSec == 0) {
                continue;
            }

            // 等待验证时长，确保服务持续运行
            std::this_thread::sleep_for(std::chrono::seconds(keepSec));

            // 检查该服务是否仍处于活跃状态
            if (!IsServiceActive(name)) {
                return MakeError("Service " + name + " is not active after " + std::to_string(keepSec) + "s");
            }
        }
        return MakeSuccess();
    }

    ResultMsg ServiceManager::ValidateModelName(const std::string &modelName) const {
        if (modelName.empty()) {
            return MakeError("Model name is empty");
        }
        // 禁止使用 .back 结尾的模型名，避免与备份命名冲突
        if (modelName.size() >= 5 && modelName.compare(modelName.size() - 5, 5, ".back") == 0) {
            return MakeError("Model name must not end with '.back': " + modelName);
        }
        return MakeSuccess();
    }

    // --- 升级辅助 ---

    bool ServiceManager::IsTarPackage(const std::string &path) const {
        // 使用后缀匹配修复旧代码 find_last_of 的 BUG
        return utils::HasSuffix(path, ".tar.gz") || utils::HasSuffix(path, ".tgz");
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg ServiceManager::PrepareUpgradeSource(const std::string &serviceName, const std::string &softwareTarPath,
                                                   const std::string &tempDir) {
        if (IsTarPackage(softwareTarPath)) {
            return ExtractSoftwareTar(softwareTarPath, tempDir, serviceName);
        }
        if (fs::is_directory(softwareTarPath)) {
            // 已解压目录：解析服务名后移动到临时目录统一处理
            // 支持两种目录结构：dir/service.yaml 或 dir/<serviceName>/service.yaml
            std::string yamlPath = utils::JoinPath(softwareTarPath, DefaultServiceName);
            if (!fs::exists(yamlPath)) {
                yamlPath = utils::JoinPath(softwareTarPath, serviceName, DefaultServiceName);
            }
            auto configServiceName = ResolveServiceName(yamlPath);
            if (configServiceName.empty() || configServiceName != serviceName) {
                return MakeError("Failed to resolve service name from directory: " + softwareTarPath);
            }
            // 使用拷贝而非移动：避免升级失败时（服务被中断）用户源目录丢失无法恢复
            auto result = utils::CopyDirectory(softwareTarPath, tempDir);
            if (!result.IsDefalutSuccess()) {
                return result;
            }
            // 确保临时目录下存在 <serviceName>/ 子目录（与 tar 包解压后结构一致）
            std::string serviceSubDir = utils::JoinPath(tempDir, serviceName);
            if (!fs::exists(serviceSubDir)) {
                // service.yaml 直接在 tempDir 下：创建 serviceName 子目录并移入所有内容
                auto mkdirRet = utils::CreateDirectory(serviceSubDir);
                if (!mkdirRet.IsDefalutSuccess()) {
                    return MakeError("Failed to create service subdirectory: " + mkdirRet.msg);
                }
                for (auto &entry : fs::directory_iterator(tempDir)) {
                    if (entry.path().filename() == serviceName) {
                        continue;
                    }
                    std::string dst = utils::JoinPath(serviceSubDir, entry.path().filename().string());
                    fs::rename(entry.path(), dst);
                }
            } else {
                utils::RenameFirstSubdirectory(tempDir, serviceName);
            }
            return MakeSuccess();
        }
        return MakeError("Invalid software path (not tar.gz or directory): " + softwareTarPath);
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg ServiceManager::BackupDatabaseIfNeeded(const std::string &serviceName) {
        auto* svc = mConfigLoader->GetServiceByName(serviceName);
        if (svc == nullptr) {
            return MakeSuccess();
        }
        // 检查依赖是否包含 mariadb/mysql
        bool dependsMariadb = false;
        for (const auto &dep : svc->dependencies) {
            if (tool::IsMariadbService(dep.first)) {
                dependsMariadb = true;
                break;
            }
        }
        if (!dependsMariadb) {
            return MakeSuccess();
        }
        // 检查 service.yaml 是否含 initDB_sql_dir
        std::string yamlPath = mFileManager->GetServiceConfigPath(serviceName);
        std::string sqlDir = utils::ReadInitSqlDir(yamlPath);
        if (sqlDir.empty()) {
            return MakeSuccess();
        }

        // 准备 db_backup 目录
        std::string backupRoot = mFileManager->GetCurDirConfig().backupDir;
        std::string dbBackupDir = utils::JoinPath(backupRoot, serviceName, "db_backup");
        auto ret = utils::CreateDirectory(dbBackupDir);
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Failed to create db_backup dir: " + ret.msg);
        }

        // 使用服务名作为 DB 用户名（与 InitServiceDatabase 约定一致）
        tool::Mariadb mariadb(tool::MariadbDef {});
        auto backupResult = mariadb.BackupUserDatabases(serviceName, dbBackupDir);
        if (!backupResult.IsDefalutSuccess()) {
            return MakeError("Failed to backup databases: " + backupResult.msg);
        }
        SLOG_INFO << "Database backup completed for service: " << serviceName << ", databases: " << backupResult.msg;
        return MakeResult(0, dbBackupDir);
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg ServiceManager::ExecuteDatabaseUpgradeScripts(const std::string &serviceName) {
        auto* svc = mConfigLoader->GetServiceByName(serviceName);
        if (svc == nullptr) {
            return MakeError("Service not found: " + serviceName);
        }
        // 读取 service.yaml 的 initDB_sql_dir
        std::string yamlPath = mFileManager->GetServiceConfigPath(serviceName);
        std::string sqlDir = utils::ReadInitSqlDir(yamlPath);
        if (sqlDir.empty()) {
            return MakeSuccess();
        }

        // 解析 SQL 脚本目录绝对路径
        std::string absSqlDir = utils::GetAbsolutePath(utils::JoinPath(svc->currentServiceDir, sqlDir));
        if (!fs::exists(absSqlDir)) {
            SLOG_WARN << "initDB_sql_dir does not exist, skip: " << absSqlDir;
            return MakeSuccess();
        }

        // 升级期必须复用安装期创建的服务账号执行 SQL，禁止使用管理账号（root）
        // 原因：1) 服务账号仅对自己前缀的库有权限，避免误改其它库；2) 管理账号风险过大
        // 服务账号约定：用户名 = serviceName，密码文件位于 <outputDir>/<serviceName>
        if (svc->dbInfo.outputDir.empty()) {
            // service.yaml 未配置 db_output_dir，无法定位密码文件
            SLOG_ERROR << "db_output_dir is empty, cannot locate service account credential";
            return MakeError("db_output_dir is empty, cannot locate service account credential");
        }

        std::string passFile = utils::JoinPath(svc->currentServiceDir, svc->dbInfo.outputDir, serviceName);
        if (!fs::exists(passFile)) {
            // 密码文件不存在（安装期未生成），无法以服务账号身份执行升级 SQL
            SLOG_ERROR << "Service account credential file not found: " << passFile;
            return MakeError("Service account credential file not found: " + passFile);
        }

        std::string dbUser = serviceName;
        std::string dbPassword;
        std::ifstream ifs(passFile);
        if (ifs.is_open()) {
            // 第一行为用户名，第二行为密码（与安装期写入格式一致）
            std::getline(ifs, dbUser);
            std::getline(ifs, dbPassword);
            ifs.close();
        }

        // 用户名或密码为空视为凭证异常，直接失败（避免误用管理账号）
        if (dbUser.empty() || dbPassword.empty()) {
            SLOG_ERROR << "Service account credential is empty in file: " << passFile << ", user=" << dbUser
                       << ", password=" << (dbPassword.empty() ? "empty" : "non-empty");
            return MakeError("Service account credential is empty in file: " + passFile);
        }

        // 执行 .sql 脚本（按字母序），统一以服务账号身份执行
        std::vector<std::string> sqlFiles;
        auto res = utils::GetAllFilesInDir(sqlFiles, absSqlDir, ".sql");
        if (!res.IsDefalutSuccess()) {
            return MakeError("Get sql files error: " + res.msg);
        }
        std::sort(sqlFiles.begin(), sqlFiles.end());

        tool::Mariadb mariadb(tool::MariadbDef {});
        for (const auto &sqlFile : sqlFiles) {
            SLOG_INFO << "Executing upgrade SQL file: " << sqlFile << " as user: " << dbUser;
            res = mariadb.ExecuteSqlFileByUser(dbUser, dbPassword, sqlFile);
            if (!res.IsDefalutSuccess()) {
                return MakeError("Execute SQL file " + sqlFile + " failed: " + res.msg);
            }
        }
        SLOG_INFO << "Database upgrade scripts executed for service: " << serviceName;
        return MakeSuccess();
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg ServiceManager::RollbackDatabase(const std::string &serviceName) {
        std::string backupRoot = mFileManager->GetCurDirConfig().backupDir;
        std::string dbBackupDir = utils::JoinPath(backupRoot, serviceName, "db_backup");
        if (!fs::exists(dbBackupDir)) {
            return MakeSuccess();
        }
        tool::Mariadb mariadb(tool::MariadbDef {});
        auto result = mariadb.RestoreUserDatabases(dbBackupDir);
        if (!result.IsDefalutSuccess()) {
            return MakeError("Failed to rollback databases: " + result.msg);
        }
        SLOG_INFO << "Database rollback completed for service: " << serviceName;
        return MakeSuccess();
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg ServiceManager::UpdateServiceWithDetail(const std::string &serviceName, const std::string &sourceDir,
                                                      const std::string &upDetailPath, bool wasRunning) {
        SLOG_INFO << "Updating service with detail: " << serviceName;

        // 1. 解析 up_detail.yaml
        UpgradeDetail detail;
        auto result = mFileManager->ParseUpgradeDetail(upDetailPath, detail);
        if (!result.IsDefalutSuccess()) {
            return MakeError("Failed to parse up_detail.yaml: " + result.msg);
        }

        // 2. 备份数据库（若依赖 mariadb 且有 initDB_sql_dir）
        auto dbBackupResult = BackupDatabaseIfNeeded(serviceName);
        if (!dbBackupResult.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to backup database: " << dbBackupResult.msg;
            DoRollbackUpgrade(serviceName, wasRunning, true);
            return MakeError("Failed to backup database: " + dbBackupResult.msg);
        }

        // 4. 细粒度文件备份
        result = mFileManager->BackupForFineGrainedUpgrade(serviceName, detail);
        if (!result.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to backup for fine-grained upgrade: " << result.msg;
            DoRollbackUpgrade(serviceName, wasRunning, true);
            return MakeError("Failed to backup for fine-grained upgrade: " + result.msg);
        }

        // 5. 执行细粒度文件升级
        result = mFileManager->ApplyFineGrainedUpgrade(serviceName, sourceDir, detail);
        if (!result.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to apply fine-grained upgrade: " << result.msg;
            DoRollbackUpgrade(serviceName, wasRunning, true);
            return MakeError("Failed to apply fine-grained upgrade: " + result.msg);
        }

        // 6. 重新加载配置（service.yaml 可能已通过 replace 更新）
        std::string installedServiceDir = mFileManager->GetServiceWDir(serviceName);
        result = mConfigLoader->UpgradeService(installedServiceDir);
        if (!result.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to upgrade service config: " << result.msg;
            DoRollbackUpgrade(serviceName, wasRunning, true);
            return MakeError("Failed to upgrade service config: " + result.msg);
        }

        // 7. 重新生成 systemd 服务文件
        result = GenerateAndCreateServiceFile(serviceName);
        if (!result.IsDefalutSuccess()) {
            DoRollbackUpgrade(serviceName, wasRunning, true);
            return result;
        }

        // 8. 执行升级 SQL 脚本（若 initDB_sql_dir 存在）
        auto sqlResult = ExecuteDatabaseUpgradeScripts(serviceName);
        if (!sqlResult.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to execute database upgrade scripts: " << sqlResult.msg;
            DoRollbackUpgrade(serviceName, wasRunning, true);
            return MakeError("Failed to execute database upgrade scripts: " + sqlResult.msg);
        }

        // 9. 启动服务并按 keep_alive_time_sec 验证，结束后恢复原状态
        auto verifyResult = VerifyAndRestoreServiceState(serviceName, wasRunning, true);
        if (!verifyResult.IsDefalutSuccess()) {
            return verifyResult;
        }

        SLOG_INFO << "Service updated with detail successfully: " << serviceName;
        return MakeSuccess();
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg ServiceManager::UpdateServiceDefault(const std::string &serviceName, const std::string &parentDir,
                                                   bool wasRunning) {
        SLOG_INFO << "Updating service with default (full) strategy: " << serviceName;

        // parentDir 为临时目录（包含 <serviceName>/ 子目录），UpgradeSoftwarePackage 内部会执行
        // JoinPath(softwareDir, serviceName) 定位实际软件包，与 InstallSoftwarePackage 路径约定一致
        auto result = mFileManager->UpgradeSoftwarePackage(serviceName, parentDir);
        if (!result.IsDefalutSuccess()) {
            DoRollbackUpgrade(serviceName, wasRunning, false);
            return MakeError("Failed to upgrade software package: " + result.msg);
        }

        // 使用 ConfigLoader::UpgradeService 更新配置
        std::string installedServiceDir = mFileManager->GetServiceWDir(serviceName);
        result = mConfigLoader->UpgradeService(installedServiceDir);
        if (!result.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to upgrade service config: " << result.msg;
            DoRollbackUpgrade(serviceName, wasRunning, false);
            return MakeError("Failed to upgrade service config: " + result.msg);
        }

        result = GenerateAndCreateServiceFile(serviceName);
        if (!result.IsDefalutSuccess()) {
            DoRollbackUpgrade(serviceName, wasRunning, false);
            return result;
        }

        // 全量升级也需要备份数据库（若依赖 mariadb 且有 initDB_sql_dir）
        auto dbBackupResult = BackupDatabaseIfNeeded(serviceName);
        if (!dbBackupResult.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to backup database: " << dbBackupResult.msg;
            DoRollbackUpgrade(serviceName, wasRunning, false);
            return MakeError("Failed to backup database: " + dbBackupResult.msg);
        }

        // 执行升级 SQL 脚本（若 initDB_sql_dir 存在），失败则整库回退
        auto sqlResult = ExecuteDatabaseUpgradeScripts(serviceName);
        if (!sqlResult.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to execute database upgrade scripts: " << sqlResult.msg;
            DoRollbackUpgrade(serviceName, wasRunning, false);
            return MakeError("Failed to execute database upgrade scripts: " + sqlResult.msg);
        }

        // 升级成功后不立即清理备份，留给上层在数据库初始化成功后再清理
        // 启动服务并按 keep_alive_time_sec 验证，结束后恢复原状态
        auto verifyResult = VerifyAndRestoreServiceState(serviceName, wasRunning, false);
        if (!verifyResult.IsDefalutSuccess()) {
            return verifyResult;
        }

        SLOG_INFO << "Service updated successfully (default): " << serviceName;
        return MakeSuccess();
    }

    ResultMsg ServiceManager::EnableAutoStart(const std::string &serviceName) {
        SLOG_INFO << "Enabling auto-start for service: " << serviceName;

        if (!mInitialized) {
            return MakeError("ServiceManager is not initialized");
        }

        auto* svc = mConfigLoader->GetServiceByName(serviceName);
        if (!svc) {
            return MakeError("Service not found: " + serviceName);
        }

        auto result = mDBusManager->EnableUnit(ToSystemdUnitName(serviceName));
        if (!result.IsDefalutSuccess()) {
            return MakeError("Failed to enable auto-start: " + result.msg);
        }

        SLOG_INFO << "Auto-start enabled successfully for service: " << serviceName;
        return MakeSuccess();
    }

    ResultMsg ServiceManager::DisableAutoStart(const std::string &serviceName) {
        SLOG_INFO << "Disabling auto-start for service: " << serviceName;

        if (!mInitialized) {
            return MakeError("ServiceManager is not initialized");
        }

        auto* svc = mConfigLoader->GetServiceByName(serviceName);
        if (!svc) {
            return MakeError("Service not found: " + serviceName);
        }

        auto result = mDBusManager->DisableUnit(ToSystemdUnitName(serviceName));
        if (!result.IsDefalutSuccess()) {
            return MakeError("Failed to disable auto-start: " + result.msg);
        }

        SLOG_INFO << "Auto-start disabled successfully for service: " << serviceName;
        return MakeSuccess();
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg ServiceManager::StartAllAutoStartServices() {
        SLOG_INFO << "Starting all auto-start services in dependency order";

        if (!mInitialized) {
            return MakeError("ServiceManager is not initialized");
        }

        auto seqResult = EnsureSequenceUpdated();
        if (!seqResult.IsDefalutSuccess()) {
            return seqResult;
        }

        for (const auto &svcName : mServiceSequence.startOrder) {
            auto* svc = mConfigLoader->GetServiceByName(svcName);
            if (!svc || !svc->isAutoStart) {
                continue;
            }

            auto stateResult = mDBusManager->GetUnitActiveState(ToSystemdUnitName(svcName));
            if (stateResult.IsDefalutSuccess() && stateResult.msg == "active") {
                SLOG_INFO << "Service already running: " << svcName;
                continue;
            }

            // 在自动启动上下文中，检查所有依赖是否允许被自动启动
            // 若某个依赖的 isAutoStart=false 且未在运行，则跳过当前服务，避免级联启动用户明确配置为不自动启动的服务
            bool canAutoStart = true;
            for (const auto &[depName, _] : svc->dependencies) {
                // 系统服务暂时跳过
                if (tool::IsMariadbService(depName)) {
                    continue;
                }
                auto* depSvc = mConfigLoader->GetServiceByName(depName);
                if (!depSvc) {
                    SLOG_WARN << "Skipping auto-start for " << svcName << ": dependency " << depName << " not found";
                    canAutoStart = false;
                    break;
                }
                if (!depSvc->isAutoStart) {
                    auto depState = mDBusManager->GetUnitActiveState(ToSystemdUnitName(depName));
                    if (!(depState.IsDefalutSuccess() && depState.msg == "active")) {
                        SLOG_WARN << "Skipping auto-start for " << svcName << ": dependency " << depName
                                  << " has autoStart=false and is not running";
                        canAutoStart = false;
                        break;
                    }
                }
            }
            if (!canAutoStart) {
                continue;
            }

            auto result = StartService(svcName);
            if (!result.IsDefalutSuccess()) {
                SLOG_ERROR << "Failed to start auto-start service " << svcName << ": " << result.msg;
            }
        }

        SLOG_INFO << "All auto-start services processed";
        return MakeSuccess();
    }

    ResultMsg ServiceManager::RegenerateAllServiceFiles() {
        SLOG_INFO << "Regenerating systemd service files for all installed services";

        if (!mInitialized) {
            return MakeError("ServiceManager is not initialized");
        }

        // GetAllServices 返回值拷贝，遍历中重新生成 .service 文件，应用新增的 systemd 配置
        auto allServices = mConfigLoader->GetAllServices();
        if (allServices.empty()) {
            SLOG_INFO << "No installed services, skip regenerating service files";
            return MakeSuccess();
        }

        std::string failedNames;
        int successCount = 0;
        for (const auto &svc : allServices) {
            // GenerateAndCreateServiceFile 会重新生成 .service 文件并 reload daemon
            auto result = GenerateAndCreateServiceFile(svc.serviceName);
            if (!result.IsDefalutSuccess()) {
                SLOG_WARN << "Failed to regenerate service file for " << svc.serviceName << ": " << result.msg;
                failedNames += svc.serviceName + ", ";
            } else {
                ++successCount;
            }
        }

        if (!failedNames.empty()) {
            // 去掉末尾的 ", "
            if (failedNames.size() >= 2 && failedNames.substr(failedNames.size() - 2) == ", ") {
                failedNames.erase(failedNames.size() - 2);
            }
            return MakeWarning("Regenerated " + std::to_string(successCount) +
                               " service files, but failed for: " + failedNames);
        }

        SLOG_INFO << "Regenerated " << successCount << " service file(s) successfully";
        return MakeSuccess();
    }

    ResultMsg ServiceManager::StopAllServices() {
        SLOG_INFO << "Stopping all running services in reverse dependency order";

        if (!mInitialized) {
            return MakeError("ServiceManager is not initialized");
        }

        auto seqResult = EnsureSequenceUpdated();
        if (!seqResult.IsDefalutSuccess()) {
            return seqResult;
        }

        for (const auto &svcName : mServiceSequence.stopOrder) {
            auto stateResult = mDBusManager->GetUnitActiveState(ToSystemdUnitName(svcName));
            if (!stateResult.IsDefalutSuccess() || stateResult.msg != "active") {
                continue;
            }

            auto result = StopService(svcName);
            if (!result.IsDefalutSuccess()) {
                SLOG_ERROR << "Failed to stop service " << svcName << ": " << result.msg;
            }
        }

        SLOG_INFO << "All running services stopped";
        return MakeSuccess();
    }

    ServiceSequence ServiceManager::GetServiceSequence() {
        EnsureSequenceUpdated();
        return mServiceSequence;
    }

    // --- 内部预置升级包辅助 ---

    ResultMsg ServiceManager::FindInternalUpgradePackage(const std::string &serviceName) {
        auto* svc = mConfigLoader->GetServiceByName(serviceName);
        if (svc == nullptr) {
            return MakeError("Service not found: " + serviceName);
        }

        // 未配置 soft_dir，无法走内部包升级
        if (svc->upgradeConfig.softDir.empty()) {
            return MakeError("Service " + serviceName + " has no upgrade.soft_dir configured");
        }

        // 基于服务的 currentServiceDir 解析 soft_dir 为绝对路径（与 db_output_dir 解析方式一致）
        std::string absSoftDir =
            utils::GetAbsolutePath(utils::JoinPath(svc->currentServiceDir, svc->upgradeConfig.softDir));
        SLOG_INFO << "Searching internal upgrade package in: " << absSoftDir;

        // 获取目录下所有 .tar.gz 文件
        std::vector<std::string> tarFiles;
        auto ret = utils::GetAllFilesInDir(tarFiles, absSoftDir, ".tar.gz");
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Failed to list upgrade packages in " + absSoftDir + ": " + ret.msg);
        }

        if (tarFiles.empty()) {
            return MakeError("No .tar.gz upgrade package found in " + absSoftDir);
        }

        // 取第一个 .tar.gz 包（排序保证选包确定性）
        // 包内服务名校验和版本递增校验由 UpdateService 内部完成
        std::sort(tarFiles.begin(), tarFiles.end());
        SLOG_INFO << "Found internal upgrade package: " << tarFiles[0];
        return MakeResult(0, tarFiles[0]);
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    std::string ServiceManager::ReadVersionFromPackage(const std::string &serviceName, const std::string &packagePath) {
        // 目录形式：直接读取 <packagePath>/service.yaml
        if (fs::is_directory(packagePath)) {
            std::string yamlPath = utils::JoinPath(packagePath, DefaultServiceName);
            std::string version = utils::ReadVersion(yamlPath);
            if (version.empty()) {
                SLOG_WARN << "Empty version from directory service.yaml: " << yamlPath;
            }
            SLOG_INFO << "Read version " << version << " from directory: " << packagePath;
            return version;
        }

        // tar 包形式：构造 tar 流式读取命令 tar -xzf <pkg> -O <serviceName>/service.yaml
        // -O 输出到 stdout，避免完整解压
        std::string innerYaml = serviceName + "/" + DefaultServiceName;
        std::string cmd = "tar -xzf '" + packagePath + "' -O '" + innerYaml + "' 2>/dev/null";

        SLOG_DEBUG << "Reading version from package: " << cmd;

        FILE* pipe = popen(cmd.c_str(), "r");
        if (pipe == nullptr) {
            SLOG_ERROR << "Failed to popen tar command: " << packagePath;
            return "";
        }

        std::string content;
        // 使用堆分配避免 4KB 缓冲区占用栈空间，触发 -Wstack-usage= 警告
        std::vector<char> buffer(4096);
        while (true) {
            size_t bytesRead = fread(buffer.data(), 1, buffer.size(), pipe);
            if (bytesRead == 0) {
                break;
            }
            content.append(buffer.data(), bytesRead);
        }
        int status = pclose(pipe);
        if (status != 0) {
            SLOG_WARN << "tar command exited with non-zero status " << status << " for package: " << packagePath;
            return "";
        }

        if (content.empty()) {
            SLOG_WARN << "Empty service.yaml content from package: " << packagePath;
            return "";
        }

        // 将 tar 输出内容写入临时文件后复用 YamlResolve 读取 version
        // 避免在内存中直接解析 YAML 格式（yaml-cpp 需要文件或流）
        std::string tempDir = utils::GenerateTempDir(mFileManager->GetCurDirConfig().tempDir);
        auto dirRet = utils::CreateDirectory(tempDir);
        if (!dirRet.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to create temp dir for version read: " << dirRet.msg;
            return "";
        }
        std::string tempYaml = utils::JoinPath(tempDir, DefaultServiceName);
        std::ofstream ofs(tempYaml, std::ios::trunc);
        if (!ofs.is_open()) {
            SLOG_ERROR << "Failed to open temp yaml file: " << tempYaml;
            utils::ForceDeleteDirectory(tempDir);
            return "";
        }
        ofs << content;
        ofs.close();

        std::string version = utils::ReadVersion(tempYaml);
        SLOG_INFO << "Read version " << version << " from package: " << packagePath;

        utils::ForceDeleteDirectory(tempDir);
        return version;
    }

}  // namespace qifeng::scm
