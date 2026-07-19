/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

// service_utils.cpp: ServiceManager 共享底层工具方法的实现
// 从 service_manager.cpp 拆分而来（Phase 6 Task 11）。
//
// 文件内含两类工具：
//   1. service_utils 命名空间下的自由函数（ToSystemdUnitName / IsTarPackage）：
//      纯工具函数，不依赖 ServiceManager 实例状态，声明位于 service_utils.h。
//   2. 仍保留为 ServiceManager 成员函数的方法（ExtractSoftwareTar/ResolveServiceName/
//      GenerateAndCreateServiceFile/ConvertActiveStateToStatus/CollectRuntimeInfo/
//      CleanupTempDirectory）：访问 mConfigLoader/mFileManager/mDBusManager 等私有成员，
//      声明保留在 service_manager.h，仅做物理拆分以降低 service_manager.cpp 体量。

#include "common/config.h"
#include "common/types.h"
#include "service_manager/service_manager.h"
#include "service_manager/service_utils.h"

#include "common/utils/file.h"
#include "common/utils/path.h"
#include "common/utils/string.h"
#include "common/utils/systemd_time.h"
#include "common/utils/yaml_resolve.h"
#include "qifeng_framework/common/logger.h"
#include "service_manager/dbus_manager.h"
#include "service_manager/file_manager.h"
#include "service_manager/service_generator.h"

#include <cstdint>

namespace qifeng::scm {

    // --- 自由工具函数（不依赖 ServiceManager 实例状态） ---

    namespace service_utils {

        std::string ToSystemdUnitName(const std::string &serviceName) {
            return std::string(FileManager::GetServiceFilePrefix()) + serviceName;
        }

        bool IsTarPackage(const std::string &path) {
            // 使用后缀匹配修复旧代码 find_last_of 的 BUG
            return utils::HasSuffix(path, ".tar.gz") || utils::HasSuffix(path, ".tgz");
        }

    }  // namespace service_utils

    // --- ServiceManager 成员工具方法（从 service_manager.cpp 迁移） ---

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
        std::string unitName = service_utils::ToSystemdUnitName(serviceName);

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

        // CPU 占用百分比
        auto cpuResult = mDBusManager->GetServiceCPUUsageNSec(unitName);
        if (cpuResult.IsDefaultSuccess() && activeEnterUsec > 0) {
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

}  // namespace qifeng::scm
