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

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <json/json.h>
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
        if (!result.IsDefaultSuccess()) {
            SLOG_ERROR << "Failed to initialize FileManager: " << result.msg;
            return;
        }

        result = InitializeDBusManager();
        if (!result.IsDefaultSuccess()) {
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
            if (!initResult.IsDefaultSuccess()) {
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

        mFileManager = std::make_shared<FileManager>(std::move(dirConfig));
        auto result = mFileManager->InitFileDir();
        if (!result.IsDefaultSuccess()) {
            return MakeError("Failed to initialize FileManager: " + result.msg);
        }

        return MakeSuccess();
    }

    ResultMsg ServiceManager::InitializeDBusManager() {
        mDBusManager = std::make_shared<DBusManager>(BusType::System);
        if (!mDBusManager->IsConnected()) {
            return MakeError("Failed to connect to systemd DBus");
        }

        // sd_bus_open_system成功不代表实际能通信，需做一次真实调用验证
        auto testResult = mDBusManager->ReloadDaemon();
        if (!testResult.IsDefaultSuccess()) {
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

    // --- 共享依赖上下文 ---

    ServiceContext ServiceManager::GetServiceContext() const {
        ServiceContext ctx;
        ctx.configLoader = mConfigLoader;
        ctx.fileManager = mFileManager;
        ctx.dbusManager = mDBusManager;
        return ctx;
    }

    // --- 共享辅助方法（供 NginxManager/ModelManager/UpgradeService 调用） ---

    std::string ServiceManager::ToSystemdUnitName(const std::string &serviceName) {
        return std::string(FileManager::GetServiceFilePrefix()) + serviceName;
    }

    ResultMsg ServiceManager::ExtractSoftwareTar(const std::string &tarPath, const std::string &extractDir,
                                                 const std::string &newName) {
        auto result = utils::ExtractTarWithCleanup(tarPath, extractDir);
        if (!result.IsDefaultSuccess()) {
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
        if (!seqResult.IsDefaultSuccess()) {
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
        if (!seqResult.IsDefaultSuccess()) {
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
            if (stateResult.IsDefaultSuccess() && stateResult.msg == "active") {
                continue;
            }

            auto startResult = mDBusManager->StartUnit(ToSystemdUnitName(name));
            if (!startResult.IsDefaultSuccess()) {
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
        if (!genResult.IsDefaultSuccess()) {
            return MakeError("Failed to generate service file content: " + genResult.msg);
        }

        // 检查 service 文件是否已存在，若存在则更新
        auto fileResult = mFileManager->CreateServiceFile(genResult.msg, serviceName);
        if (!fileResult.IsDefaultSuccess()) {
            // 文件已存在时尝试更新
            fileResult = mFileManager->FreshServiceFile(genResult.msg, serviceName);
            if (!fileResult.IsDefaultSuccess()) {
                return MakeError("Failed to create/update service file: " + fileResult.msg);
            }
        }

        auto linkResult = mFileManager->FreshServiceSymlink(serviceName);
        if (!linkResult.IsDefaultSuccess()) {
            return MakeError("Failed to fresh/update service symlink: " + linkResult.msg);
        }

        auto reloadResult = mDBusManager->ReloadDaemon();
        if (!reloadResult.IsDefaultSuccess()) {
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
            SLOG_ERROR << "Service not found: " + serviceName;
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
        if (stateResult.IsDefaultSuccess()) {
            info.status = stateResult.msg;
        }

        // 主进程 PID
        auto pidResult = mDBusManager->GetServiceMainPID(unitName);
        if (pidResult.IsDefaultSuccess()) {
            try {
                info.pid = static_cast<pid_t>(std::stoul(pidResult.msg));
            } catch (...) {
                info.pid = 0;
            }
        }

        // 重启次数
        auto nRestartsResult = mDBusManager->GetServiceNRestarts(unitName);
        if (nRestartsResult.IsDefaultSuccess()) {
            try {
                info.recoveryCount = std::stoi(nRestartsResult.msg);
            } catch (...) {
                info.recoveryCount = 0;
            }
        }

        // 内存使用量（systemd 在服务未运行时返回 UINT64_MAX，视为 0）
        auto memResult = mDBusManager->GetServiceMemoryCurrent(unitName);
        if (memResult.IsDefaultSuccess()) {
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
        if (timestampResult.IsDefaultSuccess()) {
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

        // CPU 占用比率（0.0~1.0，已按核心数归一化）
        auto cpuResult = mDBusManager->GetServiceCPUUsageNSec(unitName);
        if (cpuResult.IsDefaultSuccess() && activeEnterUsec > 0) {
            try {
                uint64_t cpuUsageNSec = std::stoull(cpuResult.msg);
                info.cpuUsage = utils::CalculateCpuUsage(cpuUsageNSec, activeEnterUsec);
            } catch (...) {
                info.cpuUsage = 0.0;
            }
        }

        // --- 3. 错误诊断信息（SubState + 失败原因） ---
        // 获取 SubState（无论服务是否 active 都采集）
        auto subStateResult = mDBusManager->GetUnitSubState(unitName);
        if (subStateResult.IsDefaultSuccess()) {
            info.subState = subStateResult.msg;
        }

        // 当服务不在 active 状态时，采集详细的错误诊断信息
        bool isActive = (info.status == "active" || info.status == "activating");
        if (!isActive) {
            auto resultResult = mDBusManager->GetServiceResult(unitName);
            if (resultResult.IsDefaultSuccess()) {
                info.errorResult = resultResult.msg;
            }

            auto exitCodeResult = mDBusManager->GetServiceExecMainCode(unitName);
            if (exitCodeResult.IsDefaultSuccess()) {
                try {
                    info.exitCode = std::stoi(exitCodeResult.msg);
                } catch (...) {
                    info.exitCode = 0;
                }
            }

            auto exitStatusResult = mDBusManager->GetServiceExecMainStatus(unitName);
            if (exitStatusResult.IsDefaultSuccess()) {
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
        if (result.IsDefaultSuccess()) {
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
        if (seqResult.IsDefaultSuccess()) {
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
            if (!result.IsDefaultSuccess()) {
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
            if (!result.IsDefaultSuccess()) {
                CleanupTempDirectory(extractDir);
                return result;
            }
            // 确保临时目录下存在 <serviceName>/ 子目录（与 tar 包解压后结构一致）
            std::string serviceSubDir = utils::JoinPath(extractDir, serviceName);
            if (!fs::exists(serviceSubDir)) {
                // service.yaml 直接在 extractDir 下：创建 serviceName 子目录并移入所有内容
                auto mkdirRet = utils::CreateDirectory(serviceSubDir);
                if (!mkdirRet.IsDefaultSuccess()) {
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
        if (!result.IsDefaultSuccess()) {
            CleanupTempDirectory(extractDir);
            return MakeError("Failed to install software package: " + result.msg);
        }

        // 注册服务到 ConfigLoader（使用安装后的服务目录）
        std::string installedServiceDir = mFileManager->GetServiceWDir(actualServiceName);
        result = mConfigLoader->AddService(installedServiceDir);
        if (!result.IsDefaultSuccess()) {
            SLOG_ERROR << "Failed to register service in ConfigLoader: " << result.msg;
            // AddService 失败时需回滚：删除已拷贝的服务目录
            mFileManager->CleanupService(actualServiceName);
            CleanupTempDirectory(extractDir);
            return MakeError("Failed to register service in ConfigLoader: " + result.msg);
        }

        result = GenerateAndCreateServiceFile(actualServiceName);
        if (!result.IsDefaultSuccess()) {
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
                std::string linkPath =
                    utils::JoinPath(installedSvc->currentServiceDir, installedSvc->modelLinkDir);
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
                    if (!mkdirRet.IsDefaultSuccess()) {
                        SLOG_ERROR << "Failed to create parent directory for model symlink: " << parentDir;
                    }
                }
                auto symlinkResult = utils::CreateSymbolicLink(targetPath, linkPath);
                if (!symlinkResult.IsDefaultSuccess()) {
                    SLOG_ERROR << "Failed to create model symlink, rolling back installation: "
                               << symlinkResult.msg;
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
            if (!startRet.IsDefaultSuccess()) {
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
        if (!depResult.IsDefaultSuccess()) {
            return depResult;
        }

        auto startDepResult = StartDependentServices(serviceName);
        if (!startDepResult.IsDefaultSuccess()) {
            return startDepResult;
        }

        // 使用 scmd_ 前缀的 systemd 单元名
        auto result = mDBusManager->StartUnit(ToSystemdUnitName(serviceName));
        if (!result.IsDefaultSuccess()) {
            return MakeError("Failed to start service: " + result.msg);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        auto stateResult = mDBusManager->GetUnitActiveState(ToSystemdUnitName(serviceName));
        // 接受 "active"（已运行）和 "activating"（启动中）两种状态为成功
        // 原因：systemd 启动某些服务（如 Type=simple 但依赖未就绪、或 Type=forking 派生中）
        //       会短暂停留在 activating，属正常过渡态，不应判定为失败
        if (stateResult.IsDefaultSuccess() && (stateResult.msg == "active" || stateResult.msg == "activating")) {
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
            if (depState.IsDefaultSuccess() && depState.msg == "active") {
                SLOG_WARN << "Warning: service " << depName << " depends on " << serviceName << " and is still running";
            }
        }

        auto result = mDBusManager->StopUnit(ToSystemdUnitName(serviceName));
        if (!result.IsDefaultSuccess()) {
            return MakeError("Failed to stop service: " + result.msg);
        }

        // 循环等待服务进入停止状态，超时时间使用配置中的 optTimeoutSec
        uint32_t timeoutSec = mConfigLoader->GetConfigInfo().optTimeoutSec;
        std::string finalState;
        for (uint32_t i = 0; i < timeoutSec * 2; ++i) {
            auto stateResult = mDBusManager->GetUnitActiveState(ToSystemdUnitName(serviceName));
            if (stateResult.IsDefaultSuccess()) {
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
        if (!pidResult.IsDefaultSuccess() || pidResult.msg.empty()) {
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
        if (finalCheck.IsDefaultSuccess() && (finalCheck.msg == "inactive" || finalCheck.msg == "failed")) {
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
        if (!result.IsDefaultSuccess()) {
            return MakeError("Failed to restart service: " + result.msg);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        auto stateResult = mDBusManager->GetUnitActiveState(ToSystemdUnitName(serviceName));
        // 与 StartService 保持一致：接受 "active" 和 "activating" 为成功
        if (stateResult.IsDefaultSuccess() && (stateResult.msg == "active" || stateResult.msg == "activating")) {
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
            if (!result.IsDefaultSuccess()) {
                return MakeError("Failed to stop services before reload: " + result.msg);
            }

            auto allServices = mConfigLoader->GetAllServices();
            for (const auto &svc : allServices) {
                result = mConfigLoader->ReloadService(svc.serviceName);
                if (!result.IsDefaultSuccess()) {
                    SLOG_ERROR << "Failed to reload service config " << svc.serviceName << ": " << result.msg;
                    continue;
                }

                result = GenerateAndCreateServiceFile(svc.serviceName);
                if (!result.IsDefaultSuccess()) {
                    SLOG_ERROR << "Failed to regenerate service file for " << svc.serviceName << ": " << result.msg;
                }
            }
            MarkSequenceDirty();

            result = StartAllAutoStartServices();
            if (!result.IsDefaultSuccess()) {
                return MakeError("Failed to start services after reload: " + result.msg);
            }
            return MakeSuccess();
        }

        // 重载单个服务：停止 -> 重新加载配置 -> 重新生成文件 -> 启动
        auto result = StopService(serviceName);
        if (!result.IsDefaultSuccess()) {
            return MakeError("Failed to stop service before reload: " + result.msg);
        }

        result = mConfigLoader->ReloadService(serviceName);
        if (!result.IsDefaultSuccess()) {
            return result;
        }

        result = GenerateAndCreateServiceFile(serviceName);
        if (!result.IsDefaultSuccess()) {
            return result;
        }

        MarkSequenceDirty();

        result = StartService(serviceName);
        if (!result.IsDefaultSuccess()) {
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
        root["cpuUsage"] = info.cpuUsage;
        root["rootPath"] = info.rootPath;
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
        return stateResult.IsDefaultSuccess() && stateResult.msg == "active";
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
        if (stateResult.IsDefaultSuccess() && stateResult.msg == "active") {
            auto stopResult = StopService(serviceName);
            if (!stopResult.IsDefaultSuccess()) {
                SLOG_INFO << "Failed to stop service during uninstall: " << stopResult.msg;
            }
        }

        // 禁用开机自启
        auto disableResult = mDBusManager->DisableUnit(ToSystemdUnitName(serviceName));
        if (!disableResult.IsDefaultSuccess()) {
            SLOG_INFO << "Failed to disable auto-start during uninstall: " << disableResult.msg;
        }

        // RemoveSoftwarePackage 内部已包含 DeleteServiceFile 和 DeleteServiceSymlink
        mFileManager->CleanupService(serviceName);

        auto removeResult = mConfigLoader->RemoveService(serviceName);
        if (!removeResult.IsDefaultSuccess()) {
            return MakeError("Failed to remove service from ConfigLoader: " + removeResult.msg);
        }

        auto reloadResult = mDBusManager->ReloadDaemon();
        if (!reloadResult.IsDefaultSuccess()) {
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
        if (stateResult.IsDefaultSuccess() && stateResult.msg == "active") {
            auto stopResult = StopService(serviceName);
            if (!stopResult.IsDefaultSuccess()) {
                SLOG_INFO << "Failed to stop service during uninstall: " << stopResult.msg;
            }
        }

        // 禁用开机自启
        auto disableResult = mDBusManager->DisableUnit(ToSystemdUnitName(serviceName));
        if (!disableResult.IsDefaultSuccess()) {
            SLOG_INFO << "Failed to disable auto-start during uninstall: " << disableResult.msg;
        }

        mFileManager->CleanupService(serviceName);
        mConfigLoader->RemoveService(serviceName);
        mDBusManager->ReloadDaemon();

        return MakeSuccess();
    }

    bool ServiceManager::IsTarPackage(const std::string &path) const {
        // 使用后缀匹配修复旧代码 find_last_of 的 BUG
        return utils::HasSuffix(path, ".tar.gz") || utils::HasSuffix(path, ".tgz");
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
        if (!result.IsDefaultSuccess()) {
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
        if (!result.IsDefaultSuccess()) {
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
        if (!seqResult.IsDefaultSuccess()) {
            return seqResult;
        }

        for (const auto &svcName : mServiceSequence.startOrder) {
            auto* svc = mConfigLoader->GetServiceByName(svcName);
            if (!svc || !svc->isAutoStart) {
                continue;
            }

            auto stateResult = mDBusManager->GetUnitActiveState(ToSystemdUnitName(svcName));
            if (stateResult.IsDefaultSuccess() && stateResult.msg == "active") {
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
                    if (!(depState.IsDefaultSuccess() && depState.msg == "active")) {
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
            if (!result.IsDefaultSuccess()) {
                SLOG_ERROR << "Failed to start auto-start service " << svcName << ": " << result.msg;
            }
        }

        SLOG_INFO << "All auto-start services processed";
        return MakeSuccess();
    }

    ResultMsg ServiceManager::StopAllServices() {
        SLOG_INFO << "Stopping all running services in reverse dependency order";

        if (!mInitialized) {
            return MakeError("ServiceManager is not initialized");
        }

        auto seqResult = EnsureSequenceUpdated();
        if (!seqResult.IsDefaultSuccess()) {
            return seqResult;
        }

        for (const auto &svcName : mServiceSequence.stopOrder) {
            auto stateResult = mDBusManager->GetUnitActiveState(ToSystemdUnitName(svcName));
            if (!stateResult.IsDefaultSuccess() || stateResult.msg != "active") {
                continue;
            }

            auto result = StopService(svcName);
            if (!result.IsDefaultSuccess()) {
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

}  // namespace qifeng::scm
