/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/types.h"
#include "service_manger/service_manager.h"

#include "common/config.h"
#include "common/scmd_def.h"
#include "common/scmd_types.h"
#include "common/utils.h"
#include "qifeng_framework/common/logger.h"
#include "service_manger/dbus_manager.h"
#include "service_manger/file_manager.h"
#include "service_manger/service_generator.h"
#include "service_tool/tools_def.h"

#include <chrono>
#include <csignal>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <json/json.h>
#include <sstream>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

    constexpr const char* TempExtractPrefix = "scmd_install_";

    // 生成唯一临时目录名，避免并发冲突
    std::string GenerateTempDirName() {
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        return TempExtractPrefix + std::to_string(getpid()) + "_" + std::to_string(now);
    }

    // 将 systemd 微秒时间戳转为格式化字符串 "YYYY-MM-DD HH:MM:SS.mmm"
    std::string FormatTimestamp(uint64_t usec) {
        auto timePoint = std::chrono::system_clock::time_point(std::chrono::microseconds(usec));
        auto timeTNow = std::chrono::system_clock::to_time_t(timePoint);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timePoint.time_since_epoch()) % 1000;

        std::stringstream ss;
        ss << std::put_time(std::localtime(&timeTNow), "%Y-%m-%d %H:%M:%S");
        ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    // 计算从 startUsec 到当前的运行时长，格式化为 "Xd HH:MM:SS"
    std::string FormatDuration(uint64_t startUsec) {
        auto now = std::chrono::system_clock::now();
        auto start = std::chrono::system_clock::time_point(std::chrono::microseconds(startUsec));
        auto diff = now - start;

        if (diff.count() <= 0) {
            return "0d 00:00:00";
        }

        auto totalSec = std::chrono::duration_cast<std::chrono::seconds>(diff).count();
        int days = static_cast<int>(totalSec / 86400);
        int hours = static_cast<int>((totalSec % 86400) / 3600);
        int minutes = static_cast<int>((totalSec % 3600) / 60);
        int seconds = static_cast<int>(totalSec % 60);

        std::stringstream ss;
        ss << days << "d ";
        ss << std::setfill('0') << std::setw(2) << hours << ":";
        ss << std::setfill('0') << std::setw(2) << minutes << ":";
        ss << std::setfill('0') << std::setw(2) << seconds;
        return ss.str();
    }

    // 计算 CPU 占用百分比：CPUUsageNSec / 运行秒数 / 1e9 * 100
    size_t CalculateCpuUsage(uint64_t cpuUsageNSec, uint64_t activeEnterUsec) {
        auto now = std::chrono::system_clock::now();
        auto start = std::chrono::system_clock::time_point(std::chrono::microseconds(activeEnterUsec));
        auto runSec = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
        if (runSec <= 0) {
            return 0;
        }
        double cpuSec = static_cast<double>(cpuUsageNSec) / 1e9;
        double percent = (cpuSec / static_cast<double>(runSec)) * 100.0;
        return static_cast<size_t>(percent);
    }

    // 生成唯一临时目录完整路径（独立函数减少栈使用）
    std::string GenerateTempExtractDir(const std::string &tempDir = "") {
        // 若tempDir是合法路径则使用，否则回退到系统临时目录
        if (!tempDir.empty()) {
            qifeng::scm::ResultMsg res = qifeng::scm::utils::CreateDirectory(tempDir);
            if (res.IsDefalutSuccess()) {
                return (fs::path(tempDir) / GenerateTempDirName()).string();
            }
        }
        return (fs::temp_directory_path() / GenerateTempDirName()).string();
    }

    // 执行解压操作（独立函数减少 ExtractSoftwareTar 的栈使用）
    qifeng::scm::ResultMsg DoExtract(const std::string &tarPath, const std::string &tempDir) {
        auto result = qifeng::scm::utils::ExtractTar(tarPath, tempDir);
        if (!result.IsDefalutSuccess()) {
            qifeng::scm::utils::ForceDeleteDirectory(tempDir);
            return qifeng::scm::MakeError("Failed to extract tar file: " + result.msg);
        }

        return result;
    }
    // 将解压后的第一个目录重命名为指定名称
    void RenameDirFirstDir(const std::string &extractDir, const std::string &newName) {
        for (auto &entry : fs::directory_iterator(extractDir)) {
            if (entry.is_directory()) {
                fs::rename(entry.path(), entry.path().parent_path() / newName);
                break;
            }
        }
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
        auto result = DoExtract(tarPath, extractDir);
        if (!result.IsDefalutSuccess()) {
            return result;
        }

        // 重命名解压后的第一个目录
        if (!newName.empty()) {
            RenameDirFirstDir(extractDir, newName);
        }

        SLOG_INFO << "Extracted tar file to: " << extractDir;
        return MakeSuccess();
    }

    // 将 InstallService 中解析服务名的逻辑提取为独立方法，降低函数复杂度和栈使用
    std::string ServiceManager::ResolveServiceName(const std::string &serviceName, const std::string &extractDir) {
        std::string yamlPath = utils::JoinPath(extractDir, serviceName, DefaultServiceName);
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

        auto genResult = ServiceGenerator::GenerateContent(*svc, FileManager::GetServiceFilePrefix());
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
                    info.startTime = FormatTimestamp(activeEnterUsec);
                    info.runTime = FormatDuration(activeEnterUsec);
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
                info.cpuUsage = CalculateCpuUsage(cpuUsageNSec, activeEnterUsec);
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
        std::string extractDir = GenerateTempExtractDir(mFileManager->GetCurDirConfig().tempDir);
        auto result = ExtractSoftwareTar(softwareTarPath, extractDir, serviceName);
        if (!result.IsDefalutSuccess()) {
            return result;
        }

        // 解析服务名：若未指定，从解压后的 service.yaml 中读取
        std::string actualServiceName = ResolveServiceName(serviceName, extractDir);
        if (actualServiceName.empty()) {
            CleanupTempDirectory(extractDir);
            return MakeError("Cannot determine service name from package");
        }

        if (actualServiceName != serviceName) {
            SLOG_ERROR << "Service name in package is " << actualServiceName << ", using name " << serviceName;
            CleanupTempDirectory(extractDir);
            return MakeError("Service name in package is " + actualServiceName + ", using name " + serviceName);
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
            // 删除已经拷贝过来的服务目录
            mConfigLoader->RemoveService(actualServiceName);
            mFileManager->CleanupService(actualServiceName);

            CleanupTempDirectory(extractDir);
            return result;
        }

        CleanupTempDirectory(extractDir);
        SLOG_INFO << "Service installed successfully: " << actualServiceName;
        MarkSequenceDirty();
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
        if (stateResult.IsDefalutSuccess() && stateResult.msg == "active") {
            SLOG_INFO << "Service started successfully: " << serviceName;
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
        if (stateResult.IsDefalutSuccess() && stateResult.msg == "active") {
            SLOG_INFO << "Service restarted successfully: " << serviceName;
            return MakeSuccess();
        } else {
            return MakeError("Service failed to restart: unexpected state " + stateResult.msg);
        }
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

        std::string extractDir = GenerateTempExtractDir(mFileManager->GetCurDirConfig().tempDir);
        auto result = ExtractSoftwareTar(softwareTarPath, extractDir, serviceName);
        if (!result.IsDefalutSuccess()) {
            return result;
        }

        // FileManager::UpgradeSoftwarePackage 内部已包含验证逻辑
        result = mFileManager->UpgradeSoftwarePackage(serviceName, extractDir);
        if (!result.IsDefalutSuccess()) {
            DoRollbackUpgrade(serviceName, wasRunning);
            CleanupTempDirectory(extractDir);
            return MakeError("Failed to upgrade software package: " + result.msg);
        }

        // 使用 ConfigLoader::UpgradeService 更新配置
        std::string installedServiceDir = mFileManager->GetServiceWDir(serviceName);
        result = mConfigLoader->UpgradeService(installedServiceDir);
        if (!result.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to upgrade service config: " << result.msg;
            DoRollbackUpgrade(serviceName, wasRunning);
            CleanupTempDirectory(extractDir);
            return MakeError("Failed to upgrade service config: " + result.msg);
        }

        result = GenerateAndCreateServiceFile(serviceName);
        if (!result.IsDefalutSuccess()) {
            DoRollbackUpgrade(serviceName, wasRunning);
            CleanupTempDirectory(extractDir);
            return result;
        }

        // 升级成功后不立即清理备份，留给上层在数据库初始化成功后再清理
        // 以便在后续步骤失败时仍可通过备份回滚

        if (wasRunning) {
            result = StartService(serviceName);
            if (!result.IsDefalutSuccess()) {
                // 启动新版本失败，尝试回滚到旧版本
                DoRollbackUpgrade(serviceName, false);
                CleanupTempDirectory(extractDir);
                return result;
            }
        }

        CleanupTempDirectory(extractDir);
        SLOG_INFO << "Service updated successfully: " << serviceName;
        MarkSequenceDirty();
        return MakeSuccess();
    }
    // NOLINTEND(readability-function-size, readability-function-cognitive-complexity)

    ResultMsg ServiceManager::CleanUpgradeBackup(const std::string &serviceName) {
        return mFileManager->CleanBackup(serviceName);
    }

    void ServiceManager::DoRollbackUpgrade(const std::string &serviceName, bool restartIfWasRunning) {
        SLOG_INFO << "Rolling back upgrade for service: " << serviceName;

        // 1. 回滚文件到旧版本
        auto rollbackResult = mFileManager->RollbackSoftwarePackage(serviceName);
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

        // 4. 如果升级前服务在运行，尝试重新启动
        if (restartIfWasRunning) {
            auto startResult = StartService(serviceName);
            if (!startResult.IsDefalutSuccess()) {
                SLOG_ERROR << "Failed to restart service after rollback: " << startResult.msg;
            }
        }

        // 5. 回滚完成后清理备份
        auto cleanResult = mFileManager->CleanBackup(serviceName);
        if (!cleanResult.IsDefalutSuccess()) {
            SLOG_WARN << "Failed to clean backup after rollback: " << cleanResult.msg;
        }

        SLOG_INFO << "Upgrade rollback completed for service: " << serviceName;
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
