/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "service_manager/upgrade_service.h"

#include "common/config.h"
#include "common/scmd_def.h"
#include "common/utils/file.h"
#include "common/utils/path.h"
#include "common/utils/string.h"
#include "common/utils/time.h"
#include "common/utils/upgrade_result.h"
#include "common/utils/version.h"
#include "common/utils/yaml_resolve.h"
#include "qifeng_framework/common/logger.h"
#include "service_manager/database_service.h"
#include "service_manager/dbus_manager.h"
#include "service_manager/file_manager.h"
#include "service_manager/model_manager.h"
#include "service_manager/nginx_manager.h"
#include "service_manager/service_context.h"
#include "service_manager/service_manager.h"
#include "service_manager/service_utils.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace qifeng::scm {

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

    // --- 升级素材查找与清理（一体化升级专用辅助） ---

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg UpgradeService::FindUpgradeArtifacts(const std::string &serviceName, const std::string &srcPath,
                                                   UpgradeArtifacts &artifacts) {
        artifacts = UpgradeArtifacts {};

        // 确定扫描目录：srcPath 为空时用服务内部 soft_dir；tar 包时先解压
        std::string scanDir;
        std::string tempDir;

        if (srcPath.empty()) {
            // 从服务内部 soft_dir 扫描
            auto* svc = mCtx.configLoader->GetServiceByName(serviceName);
            if (svc == nullptr) {
                return MakeError("Service not found: " + serviceName);
            }
            if (svc->upgradeConfig.softDir.empty()) {
                return MakeError("Service " + serviceName + " has no upgrade.soft_dir configured");
            }
            scanDir = utils::GetAbsolutePath(utils::JoinPath(svc->currentServiceDir, svc->upgradeConfig.softDir));
            SLOG_INFO << "Scanning internal soft_dir: " << scanDir;
        } else if (service_utils::IsTarPackage(srcPath)) {
            // tar 包：解压到临时目录
            tempDir = utils::GenerateTempDir(mCtx.fileManager->GetCurDirConfig().tempDir);
            auto extractRet = mServiceManager.ExtractSoftwareTar(srcPath, tempDir);
            if (!extractRet.IsDefaultSuccess()) {
                mServiceManager.CleanupTempDirectory(tempDir);
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
                if (fs::is_directory(entry.path()) || service_utils::IsTarPackage(name)) {
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
                mServiceManager.CleanupTempDirectory(tempDir);
                artifacts.tempDir.clear();
            }
            return MakeError("No upgrade artifacts found in: " + scanDir);
        }

        return MakeSuccess();
    }

    void UpgradeService::CleanupArtifactsTempDir(UpgradeArtifacts &artifacts) {
        if (!artifacts.tempDir.empty()) {
            mServiceManager.CleanupTempDirectory(artifacts.tempDir);
            artifacts.tempDir.clear();
        }
    }

    // --- 升级包版本读取（一体化升级专用辅助） ---

    // 注：ReadVersionFromPackage 的实现保留在 upgrade_service.cpp（被 UpdateService 共用）
    // --- 一体化升级 ---

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg UpgradeService::PerformIntegratedUpgrade(const std::string &serviceName, const std::string &tarDir) {
        SLOG_INFO << "Perform integrated upgrade for service: " << serviceName
                  << ", tarDir: " << (tarDir.empty() ? "<internal soft_dir>" : tarDir);

        // 解析升级结果路径（仅在配置了 upgrade.result_path 时记录结果）
        auto* svc = mCtx.configLoader->GetServiceByName(serviceName);
        if (svc == nullptr) {
            return MakeError("Service not found: " + serviceName);
        }
        std::string absResultPath;
        if (!svc->upgradeConfig.resultPath.empty()) {
            absResultPath =
                utils::GetAbsolutePath(utils::JoinPath(svc->currentServiceDir, svc->upgradeConfig.resultPath));
            SLOG_INFO << "Upgrade result will be written to: " << absResultPath;
        } else {
            SLOG_INFO << "No upgrade.result_path configured, skip result file writing";
        }

        std::string upgradeTime = utils::GetCurrentTimeString();
        std::string newVersion;
        std::string modelInstalledName;  // 非空表示已安装模型（需在失败时回退、成功时清理备份）

        // 辅助：写结果文件（失败不影响返回值，仅告警）
        auto writeResult = [&](bool success, const std::string &reason) {
            if (absResultPath.empty()) {
                return;
            }
            auto writeRet = utils::WriteUpgradeResult(absResultPath, success, upgradeTime, newVersion, reason);
            if (!writeRet.IsDefaultSuccess()) {
                SLOG_WARN << "Failed to write upgrade result file: " << writeRet.msg;
            }
        };

        // 辅助：恢复 nginx 正常配置（流程失败时调用）
        auto restoreNginx = [&]() {
            SLOG_INFO << "Restoring nginx to normal mode";
            auto ret = mNginxManager.ResetNginx(NginxResetMode::NORMAL);
            if (!ret.IsDefaultSuccess()) {
                SLOG_ERROR << "Failed to restore nginx: " << ret.msg;
            }
        };

        // 1. 进入 nginx 等待页面（所有路由返回404，避免升级期间访问到不一致状态）
        SLOG_INFO << "Step 1: Set nginx to waiting mode";
        auto waitRet = mNginxManager.ResetNginx(NginxResetMode::WAIT);
        if (!waitRet.IsDefaultSuccess()) {
            SLOG_ERROR << "Failed to set nginx waiting mode: " << waitRet.msg;
            // nginx 未进入等待态，直接返回，不继续后续流程
            return waitRet;
        }

        // 1.1 停止服务（若运行中）：升级期间服务必须停止，保证语义一致（升级一定停启）
        //     记录原始运行状态，在所有退出路径上恢复
        bool wasRunning = mServiceManager.IsServiceActive(serviceName);
        if (wasRunning) {
            SLOG_INFO << "Step 1.1: Stop running service before upgrade";
            auto stopRet = mServiceManager.StopService(serviceName);
            if (!stopRet.IsDefaultSuccess()) {
                SLOG_ERROR << "Failed to stop service before upgrade: " << stopRet.msg;
                restoreNginx();
                return stopRet;
            }
        } else {
            SLOG_INFO << "Step 1.1: Service not running, skip stop";
        }

        // 辅助：恢复服务运行状态（在所有退出路径上调用，确保升级前运行的服务在结束后恢复运行）
        auto restoreService = [&]() {
            if (wasRunning) {
                SLOG_INFO << "Restoring service to running state: " << serviceName;
                auto startRet = mServiceManager.StartService(serviceName);
                if (!startRet.IsDefaultSuccess()) {
                    SLOG_ERROR << "Failed to restore service running state: " << startRet.msg;
                }
            }
        };

        // 2. 查找升级素材（服务包/model/nginx）
        //    使用 RAII 确保临时解压目录在所有退出路径上被清理
        SLOG_INFO << "Step 2: Find upgrade artifacts";
        UpgradeArtifacts artifacts;
        auto findRet = FindUpgradeArtifacts(serviceName, tarDir, artifacts);
        if (!findRet.IsDefaultSuccess()) {
            SLOG_ERROR << "Find upgrade artifacts failed: " << findRet.msg;
            restoreNginx();
            restoreService();
            writeResult(false, findRet.msg);
            return findRet;
        }
        // RAII 守卫：方法结束时清理素材临时目录（仅当 -d 为 tar 包时 tempDir 非空）
        struct TempDirGuard {
            UpgradeService &us;
            UpgradeArtifacts &arts;
            explicit TempDirGuard(UpgradeService &u, UpgradeArtifacts &a) : us(u), arts(a) {}
            ~TempDirGuard() { us.CleanupArtifactsTempDir(arts); }
            TempDirGuard(const TempDirGuard &) = delete;
            TempDirGuard &operator=(const TempDirGuard &) = delete;
            TempDirGuard(TempDirGuard &&) = delete;
            TempDirGuard &operator=(TempDirGuard &&) = delete;
        } tempDirGuard(*this, artifacts);
        SLOG_INFO << "Artifacts found - servicePackage: "
                  << (artifacts.servicePackage.empty() ? "<none>" : artifacts.servicePackage)
                  << ", modelPath: " << (artifacts.modelPath.empty() ? "<none>" : artifacts.modelPath)
                  << ", nginxDir: " << (artifacts.nginxDir.empty() ? "<none>" : artifacts.nginxDir);

        // 3. 若有 model 素材：安装模型（排除当前升级服务，保留 .back 备份以便回退）
        if (!artifacts.modelPath.empty()) {
            SLOG_INFO << "Step 3: Add model (excluding service: " << serviceName << ")";
            auto modelRet = mModelManager.AddModelWithBackupRetained(artifacts.modelPath, serviceName, modelInstalledName);
            if (!modelRet.IsDefaultSuccess()) {
                SLOG_ERROR << "Add model failed: " << modelRet.msg;
                restoreNginx();
                restoreService();
                writeResult(false, modelRet.msg);
                return modelRet;
            }
            SLOG_INFO << "Model added successfully: " << modelInstalledName << " (backup retained)";
        } else {
            SLOG_INFO << "Step 3: No model artifact, skip";
        }

        // 4. 若有服务包：执行服务升级，失败则回退模型
        //    注意：服务已在 Step 1.1 停止，UpdateService 内部检测到服务未运行（wasRunning=false），
        //          不会重复停止；若 keep_alive_time_sec > 0，UpdateService 会启动验证后因内部
        //          wasRunning=false 而停止服务，最终由 restoreService() 恢复运行状态。
        ResultMsg result = MakeSuccess();
        if (!artifacts.servicePackage.empty()) {
            SLOG_INFO << "Step 4: Upgrade service from package: " << artifacts.servicePackage;
            // 解析新版本号（用于结果记录，失败不影响升级流程）
            newVersion = ReadVersionFromPackage(serviceName, artifacts.servicePackage);
            SLOG_INFO << "New version from package: " << (newVersion.empty() ? "<unknown>" : newVersion);

            result = UpdateService(serviceName, artifacts.servicePackage);
            if (!result.IsDefaultSuccess()) {
                SLOG_ERROR << "Upgrade service failed: " << result.msg << ", rolling back model if any";
                // 回退模型（若已安装）
                if (!modelInstalledName.empty()) {
                    auto rollbackRet = mModelManager.RollbackModel(modelInstalledName);
                    if (!rollbackRet.IsDefaultSuccess()) {
                        SLOG_ERROR << "Rollback model failed: " << rollbackRet.msg;
                    }
                }
                restoreNginx();
                restoreService();
                writeResult(false, result.msg);
                return result;
            }
            SLOG_INFO << "Service upgraded successfully";
        } else {
            SLOG_INFO << "Step 4: No service package, skip service upgrade";
        }

        // 5. 成功收尾：清理模型备份，更新或恢复 nginx 配置
        SLOG_INFO << "Step 5: Finalize - clean model backup, update nginx";
        if (!modelInstalledName.empty()) {
            auto cleanRet = mModelManager.CleanModelBackup(modelInstalledName);
            if (!cleanRet.IsDefaultSuccess()) {
                SLOG_WARN << "Failed to clean model backup: " << cleanRet.msg;
            }
        }

        if (!artifacts.nginxDir.empty()) {
            // 有 nginx 素材：使用第一个 nginx 前缀目录更新配置
            // 注意：此处失败不回滚已升级的服务/模型（核心升级已成功），仅恢复 nginx 旧配置并返回警告
            SLOG_INFO << "Updating nginx config from: " << artifacts.nginxDir;
            auto nginxRet = mNginxManager.InitNginx(artifacts.nginxDir);
            if (!nginxRet.IsDefaultSuccess()) {
                SLOG_ERROR << "InitNginx failed: " << nginxRet.msg << ", fallback to reset_nginx -n";
                restoreNginx();
                restoreService();
                std::string warnMsg = "Service upgraded successfully, but nginx config update failed: " + nginxRet.msg +
                                      " (nginx restored to previous config)";
                writeResult(false, warnMsg);
                SLOG_WARN << warnMsg;
                return MakeWarning(warnMsg);
            }
            SLOG_INFO << "Nginx config updated successfully";
        } else {
            // 无 nginx 素材：恢复 nginx 正常配置（移除 waiting.conf，使 scm_*.conf 生效）
            SLOG_INFO << "No nginx artifact, restore nginx to normal mode";
            restoreNginx();
        }

        // 恢复服务运行状态（升级前运行则启动，未运行则保持停止）
        restoreService();

        writeResult(true, "");
        SLOG_INFO << "Integrated upgrade completed successfully for service: " << serviceName;
        return MakeSuccess();
    }

}  // namespace qifeng::scm