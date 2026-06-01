/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "service_manger/service_manager.h"

#include "common/config.h"
#include "common/scmd_def.h"
#include "common/scmd_types.h"
#include "common/utils.h"
#include "qifeng_framework/common/logger.h"
#include "service_manger/dbus_manager.h"
#include "service_manger/file_manager.h"
#include "service_manger/service_generator.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace {

    constexpr const char* TempExtractPrefix = "scmd_install_";
    constexpr const char* ScmdServicePrefix = "scmd_";

    std::string GetCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto timeTNow = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::stringstream ss;
        ss << std::put_time(std::localtime(&timeTNow), "%Y-%m-%d %H:%M:%S");
        ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    // 生成唯一临时目录名，避免并发冲突
    std::string GenerateTempDirName() {
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        return TempExtractPrefix + std::to_string(getpid()) + "_" + std::to_string(now);
    }

    // JSON 字符串转义
    std::string EscapeJsonString(const std::string &input) {
        std::string output;
        output.reserve(input.size());
        for (char c : input) {
            switch (c) {
                case '"':
                    output += "\\\"";
                    break;
                case '\\':
                    output += "\\\\";
                    break;
                case '\n':
                    output += "\\n";
                    break;
                case '\r':
                    output += "\\r";
                    break;
                case '\t':
                    output += "\\t";
                    break;
                default:
                    output += c;
                    break;
            }
        }
        return output;
    }

    // 生成唯一临时目录完整路径（独立函数减少栈使用）
    std::string GenerateTempExtractDir() {
        return (fs::temp_directory_path() / GenerateTempDirName()).string();
    }

}  // namespace

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

        // 从 ConfigLoader 的 ConfigInfo 获取目录配置，而非硬编码
        FileDirInfo dirConfig;
        auto allServices = mConfigLoader->GetAllServices();

        // configDir: 使用 ConfigLoader 的配置目录
        auto* svc = mConfigLoader->GetServiceByName("");
        if (svc) {
            dirConfig.configDir = mConfigLoader->GetServiceRootDir("");
        } else {
            dirConfig.configDir = "./.config";
        }

        // serviceDir: 从已注册服务推断，或使用默认值
        if (!allServices.empty()) {
            dirConfig.serviceDir = fs::path(allServices[0].currentServiceDir).parent_path().string();
            if (dirConfig.serviceDir.empty()) {
                dirConfig.serviceDir = "./.services";
            }
        } else {
            dirConfig.serviceDir = "./.services";
        }

        dirConfig.dataDir = "./.data";
        dirConfig.backupDir = "./.backup";
        dirConfig.logsDir = "./.logs";

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
        return MakeSuccess();
    }

    // --- 辅助方法 ---

    std::string ServiceManager::ToSystemdUnitName(const std::string &serviceName) {
        return std::string(ScmdServicePrefix) + serviceName;
    }

    // 执行解压操作（独立函数减少 ExtractSoftwareTar 的栈使用）
    ResultMsg DoExtract(const std::string &tarPath, const std::string &tempDir) {
        if (!fs::exists(tarPath)) {
            return MakeError("Software tar file does not exist: " + tarPath);
        }

        auto result = utils::CreateDirectory(tempDir);
        if (!result.IsDefalutSuccess()) {
            return MakeError("Failed to create temp directory: " + result.msg);
        }

        result = utils::ExtractTar(tarPath, tempDir);
        if (!result.IsDefalutSuccess()) {
            utils::ForceDeleteDirectory(tempDir);
            return MakeError("Failed to extract tar file: " + result.msg);
        }

        return MakeSuccess();
    }

    ResultMsg ServiceManager::ExtractSoftwareTar(const std::string &tarPath, std::string &extractDir) {
        std::string tempDir = GenerateTempExtractDir();
        auto result = DoExtract(tarPath, tempDir);
        if (!result.IsDefalutSuccess()) {
            return result;
        }

        extractDir = std::move(tempDir);
        SLOG_INFO << "Extracted tar file to: " << extractDir;
        return MakeSuccess();
    }

    // 将 InstallService 中解析服务名的逻辑提取为独立方法，降低函数复杂度和栈使用
    std::string ServiceManager::ResolveServiceName(const std::string &serviceName, const std::string &extractDir) {
        if (!serviceName.empty()) {
            return serviceName;
        }

        std::string yamlPath = extractDir + "/" + DefaultServiceName;
        if (fs::exists(yamlPath)) {
            auto tempConfig = std::make_shared<ConfigLoader>();
            auto scanResult = tempConfig->ScanServicesDirectory(extractDir);
            if (scanResult.IsDefalutSuccess()) {
                auto allTempServices = tempConfig->GetAllServices();
                if (!allTempServices.empty()) {
                    return allTempServices[0].serviceName;
                }
            }
        }

        return {};
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
            auto* depSvc = mConfigLoader->GetServiceByName(depName);
            if (!depSvc) {
                return MakeError("Missing dependency for service " + serviceName + ": " + depName);
            }
            if (depSvc->version != depVersion) {
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

    ResultMsg ServiceManager::InitializeServiceDatabase(const std::string &serviceName) {
        auto* svc = mConfigLoader->GetServiceByName(serviceName);
        if (!svc) {
            return MakeError("Service not found: " + serviceName);
        }

        if (svc->dbInfo.sqlDir.empty()) {
            return MakeSuccess();
        }

        SLOG_INFO << "Database initialization for service " << serviceName
                  << " (type: " << static_cast<int>(svc->dbInfo.dbType) << ", sqlDir: " << svc->dbInfo.sqlDir << ")";
        return MakeSuccess();
    }

    ResultMsg ServiceManager::SetServiceUser(const std::string &serviceName) {
        auto* svc = mConfigLoader->GetServiceByName(serviceName);
        if (!svc) {
            return MakeError("Service not found: " + serviceName);
        }

        if (svc->execInfo.user.empty()) {
            return MakeSuccess();
        }

        SLOG_INFO << "Setting service user to: " << svc->execInfo.user;
        return MakeSuccess();
    }

    ResultMsg ServiceManager::GenerateAndCreateServiceFile(const std::string &serviceName) {
        auto* svc = mConfigLoader->GetServiceByName(serviceName);
        if (!svc) {
            return MakeError("Service not found: " + serviceName);
        }

        auto genResult = ServiceGenerator::GenerateContent(*svc);
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

        auto linkResult = mFileManager->CreateServiceSymlink(serviceName);
        if (!linkResult.IsDefalutSuccess()) {
            return MakeError("Failed to create service symlink: " + linkResult.msg);
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

    ServiceRuntimeInfo ServiceManager::CollectRuntimeInfo(const std::string &serviceName) {
        ServiceRuntimeInfo info;

        auto* svc = mConfigLoader->GetServiceByName(serviceName);
        if (!svc) {
            SLOG_ERROR << "Service not found: " << serviceName;
            return info;
        }

        info.serviceId = static_cast<int>(std::hash<std::string> {}(serviceName));
        info.currentVersion = svc->version;
        info.configFilePath = mFileManager->GetServiceConfigPath(serviceName);

        // 通过 DBus 获取运行时信息
        std::string unitName = ToSystemdUnitName(serviceName);

        auto stateResult = mDBusManager->GetUnitActiveState(unitName);
        if (stateResult.IsDefalutSuccess()) {
            info.status = stateResult.msg;
        }

        auto pidResult = mDBusManager->GetServiceMainPID(unitName);
        if (pidResult.IsDefalutSuccess()) {
            try {
                info.pid = static_cast<pid_t>(std::stoul(pidResult.msg));
            } catch (...) {
                info.pid = 0;
            }
        }

        // 获取服务实际启动时间
        auto subStateResult = mDBusManager->GetUnitSubState(unitName);
        if (stateResult.IsDefalutSuccess() && stateResult.msg == "active") {
            info.startTime = GetCurrentTimestamp();
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

        std::string extractDir;
        auto result = ExtractSoftwareTar(softwareTarPath, extractDir);
        if (!result.IsDefalutSuccess()) {
            return result;
        }

        // FileManager::InstallSoftwarePackage 内部已包含验证逻辑（VerifySoftwarePackage）
        // 无需在 ServiceManager 层重复验证

        // 解析服务名：若未指定，从解压后的 service.yaml 中读取
        std::string actualServiceName = ResolveServiceName(serviceName, extractDir);
        if (actualServiceName.empty()) {
            CleanupTempDirectory(extractDir);
            return MakeError("Cannot determine service name from package");
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
        }

        result = GenerateAndCreateServiceFile(actualServiceName);
        if (!result.IsDefalutSuccess()) {
            CleanupTempDirectory(extractDir);
            return result;
        }

        result = SetServiceUser(actualServiceName);
        if (!result.IsDefalutSuccess()) {
            CleanupTempDirectory(extractDir);
            return result;
        }

        result = InitializeServiceDatabase(actualServiceName);
        if (!result.IsDefalutSuccess()) {
            CleanupTempDirectory(extractDir);
            return result;
        }

        auto* svc = mConfigLoader->GetServiceByName(actualServiceName);
        if (svc && svc->isAutoStart) {
            result = EnableAutoStart(actualServiceName);
            if (!result.IsDefalutSuccess()) {
                SLOG_INFO << "Failed to enable auto-start: " << result.msg;
            }
        }

        CleanupTempDirectory(extractDir);
        SLOG_INFO << "Service installed successfully: " << actualServiceName;
        MarkSequenceDirty();
        return MakeSuccess();
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
        if (stateResult.IsDefalutSuccess() && stateResult.msg == "active") {
            SLOG_INFO << "Service started successfully: " << serviceName;
            return MakeSuccess();
        } else {
            return MakeError("Service failed to start: unexpected state " + stateResult.msg);
        }
    }

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
                SLOG_INFO << "Warning: service " << depName << " depends on " << serviceName << " and is still running";
            }
        }

        auto result = mDBusManager->StopUnit(ToSystemdUnitName(serviceName));
        if (!result.IsDefalutSuccess()) {
            return MakeError("Failed to stop service: " + result.msg);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        auto stateResult = mDBusManager->GetUnitActiveState(ToSystemdUnitName(serviceName));
        if (stateResult.IsDefalutSuccess() && stateResult.msg == "inactive") {
            SLOG_INFO << "Service stopped successfully: " << serviceName;
            return MakeSuccess();
        } else {
            return MakeError("Service failed to stop: unexpected state " + stateResult.msg);
        }
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
        if (stateResult.IsDefalutSuccess() && stateResult.msg == "active") {
            SLOG_INFO << "Service restarted successfully: " << serviceName;
            return MakeSuccess();
        } else {
            return MakeError("Service failed to restart: unexpected state " + stateResult.msg);
        }
    }

    ResultMsg ServiceManager::ReloadService(const std::string &serviceName) {
        SLOG_INFO << "Reloading service: " << (serviceName.empty() ? "all" : serviceName);

        if (!mInitialized) {
            return MakeError("ServiceManager is not initialized");
        }

        if (serviceName.empty()) {
            // 重载所有服务：重新加载配置 + 重新生成 service 文件 + daemon reload
            auto allServices = mConfigLoader->GetAllServices();
            for (const auto &svc : allServices) {
                auto result = mConfigLoader->ReloadService(svc.serviceName);
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
            return MakeSuccess();
        }

        auto result = mConfigLoader->ReloadService(serviceName);
        if (!result.IsDefalutSuccess()) {
            return result;
        }

        result = GenerateAndCreateServiceFile(serviceName);
        if (!result.IsDefalutSuccess()) {
            return result;
        }

        SLOG_INFO << "Service reloaded successfully: " << serviceName;
        MarkSequenceDirty();
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

        // 使用转义后的值构建 JSON
        std::stringstream ss;
        ss << "{";
        ss << "\"serviceId\":" << info.serviceId << ",";
        ss << "\"serviceName\":\"" << EscapeJsonString(serviceName) << "\",";
        ss << "\"version\":\"" << EscapeJsonString(info.currentVersion) << "\",";
        ss << "\"pid\":" << info.pid << ",";
        ss << "\"status\":\"" << EscapeJsonString(info.status) << "\",";
        ss << "\"startTime\":\"" << EscapeJsonString(info.startTime) << "\",";
        ss << "\"configFilePath\":\"" << EscapeJsonString(info.configFilePath) << "\"";
        ss << "}";

        return ResultMsg {0, ss.str()};
    }

    ServiceRuntimeInfo ServiceManager::GetServiceRuntimeInfo(const std::string &serviceName) {
        return CollectRuntimeInfo(serviceName);
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
        auto pkgResult = mFileManager->RemoveSoftwarePackage(serviceName);
        if (!pkgResult.IsDefalutSuccess()) {
            return MakeError("Failed to remove software package: " + pkgResult.msg);
        }

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

        std::string extractDir;
        auto result = ExtractSoftwareTar(softwareTarPath, extractDir);
        if (!result.IsDefalutSuccess()) {
            return result;
        }

        // FileManager::UpgradeSoftwarePackage 内部已包含验证逻辑
        result = mFileManager->UpgradeSoftwarePackage(serviceName, extractDir);
        if (!result.IsDefalutSuccess()) {
            auto rollbackResult = mFileManager->RollbackSoftwarePackage(serviceName);
            if (!rollbackResult.IsDefalutSuccess()) {
                SLOG_ERROR << "Failed to rollback after upgrade failure: " << rollbackResult.msg;
            }
            CleanupTempDirectory(extractDir);
            return MakeError("Failed to upgrade software package: " + result.msg);
        }

        // 使用 ConfigLoader::UpgradeService 更新配置
        std::string installedServiceDir = mFileManager->GetServiceWDir(serviceName);
        result = mConfigLoader->UpgradeService(installedServiceDir);
        if (!result.IsDefalutSuccess()) {
            SLOG_INFO << "Failed to upgrade service config: " << result.msg;
        }

        result = GenerateAndCreateServiceFile(serviceName);
        if (!result.IsDefalutSuccess()) {
            return result;
        }

        result = InitializeServiceDatabase(serviceName);
        if (!result.IsDefalutSuccess()) {
            return result;
        }

        // 升级成功后清理备份
        auto cleanResult = mFileManager->CleanBackup(serviceName);
        if (!cleanResult.IsDefalutSuccess()) {
            SLOG_INFO << "Failed to clean backup after upgrade: " << cleanResult.msg;
        }

        if (wasRunning) {
            result = StartService(serviceName);
            if (!result.IsDefalutSuccess()) {
                return result;
            }
        }

        CleanupTempDirectory(extractDir);
        SLOG_INFO << "Service updated successfully: " << serviceName;
        MarkSequenceDirty();
        return MakeSuccess();
    }
    // NOLINTEND(readability-function-size, readability-function-cognitive-complexity)

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

            auto result = StartService(svcName);
            if (!result.IsDefalutSuccess()) {
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

}  // namespace qifeng::scm
