/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "service_manger/upgrade_service.h"

#include "common/config.h"
#include "common/scmd_def.h"
#include "common/scmd_types.h"
#include "common/types.h"
#include "common/utils.h"
#include "common/utils/file.h"
#include "common/utils/string.h"
#include "common/utils/yaml_resolve.h"
#include "qifeng_framework/common/logger.h"
#include "service_manger/file_manager.h"
#include "service_manger/database_service.h"
#include "service_manger/model_manager.h"
#include "service_manger/nginx_manager.h"
#include "service_manger/service_context.h"
#include "service_manger/service_manager.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace qifeng::scm {

    UpgradeService::UpgradeService(const ServiceContext &ctx, ServiceManager &serviceManager,
                                   ModelManager &modelManager, NginxManager &nginxManager, DatabaseService &dbService)
        : mCtx(ctx), mServiceManager(serviceManager), mModelManager(modelManager), mNginxManager(nginxManager),
          mDbService(dbService) {
        SLOG_INFO << "UpgradeService initialized";
    }

    UpgradeService::~UpgradeService() {
        SLOG_INFO << "UpgradeService destroyed";
    }

    // NOLINTBEGIN(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg UpgradeService::UpdateService(const std::string &serviceName, const std::string &softwareTarPath) {
        SLOG_INFO << "Updating service: " << serviceName << " with: " << softwareTarPath;

        auto* svc = mCtx.configLoader->GetServiceByName(serviceName);
        if (svc == nullptr) {
            return MakeError("Service not found: " + serviceName);
        }

        // 记录服务是否正在运行，更新后恢复
        bool wasRunning = mServiceManager.IsServiceActive(serviceName);

        if (wasRunning) {
            auto stopResult = mServiceManager.StopService(serviceName);
            if (!stopResult.IsDefalutSuccess()) {
                return MakeError("Failed to stop service for update: " + stopResult.msg);
            }
        }

        std::string tempDir = utils::GenerateTempDir(mCtx.fileManager->GetCurDirConfig().tempDir);
        auto result = PrepareUpgradeSource(serviceName, softwareTarPath, tempDir);
        if (!result.IsDefalutSuccess()) {
            mServiceManager.CleanupTempDirectory(tempDir);
            return result;
        }

        // 版本号递增校验：新版本必须严格大于当前已安装版本，阻止降级和平级覆盖
        std::string sourceDir = utils::JoinPath(tempDir, serviceName);
        std::string newYamlPath = utils::JoinPath(sourceDir, DefaultServiceName);
        std::string newVersion = utils::ReadVersion(newYamlPath);
        if (newVersion.empty()) {
            mServiceManager.CleanupTempDirectory(tempDir);
            return MakeError("Failed to read version from new package: " + newYamlPath);
        }
        if (utils::CompareVersion(newVersion, svc->version) <= 0) {
            mServiceManager.CleanupTempDirectory(tempDir);
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

        mServiceManager.CleanupTempDirectory(tempDir);
        if (result.IsDefalutSuccess()) {
            SLOG_INFO << "Service updated successfully: " << serviceName;
            mServiceManager.MarkSequenceDirty();
        }
        return result;
    }
    // NOLINTEND(readability-function-size, readability-function-cognitive-complexity)

    ResultMsg UpgradeService::CleanUpgradeBackup(const std::string &serviceName) {
        return mCtx.fileManager->CleanBackup(serviceName);
    }

    ResultMsg UpgradeService::VerifyAndRestoreServiceState(const std::string &serviceName, bool wasRunning,
                                                           bool useFineGrained) {
        // 读取升级后服务配置中的 keep_alive_time_sec
        auto* upgradedSvc = mCtx.configLoader->GetServiceByName(serviceName);
        uint32_t keepSec = upgradedSvc ? upgradedSvc->keepAliveTimeSec : 0;
        if (keepSec == 0) {
            // 未配置验证时长：仅按升级前状态恢复（原未运行则保持停止，原运行则保持运行）
            if (wasRunning) {
                auto startRet = mServiceManager.StartService(serviceName);
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
        auto startRet = mServiceManager.StartService(serviceName);
        if (!startRet.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to start service after upgrade: " << startRet.msg;
            DoRollbackUpgrade(serviceName, wasRunning, useFineGrained);
            return startRet;
        }

        // 等待验证时长，检查服务是否持续运行
        std::this_thread::sleep_for(std::chrono::seconds(keepSec));
        if (!mServiceManager.IsServiceActive(serviceName)) {
            SLOG_ERROR << "Service not active after " << keepSec << "s verification";
            DoRollbackUpgrade(serviceName, wasRunning, useFineGrained);
            return MakeError("Service not active after " + std::to_string(keepSec) + "s verification");
        }
        SLOG_INFO << "Service kept alive for " << keepSec << "s: " << serviceName;

        // 验证通过：若升级前未运行，则停止服务恢复原状态
        if (!wasRunning) {
            auto stopRet = mServiceManager.StopService(serviceName);
            if (!stopRet.IsDefalutSuccess()) {
                SLOG_WARN << "Failed to restore stopped state after verification: " << stopRet.msg;
                // 停止失败不视为升级失败，仅告警（服务已验证可用）
            }
        }
        return MakeSuccess();
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    void UpgradeService::DoRollbackUpgrade(const std::string &serviceName, bool restartIfWasRunning,
                                           bool useFineGrained) {
        SLOG_INFO << "Rolling back upgrade for service: " << serviceName << " (fineGrained=" << useFineGrained << ")";

        // 1. 回滚文件到旧版本
        ResultMsg rollbackResult;
        if (useFineGrained) {
            rollbackResult = mCtx.fileManager->RollbackFineGrainedUpgrade(serviceName);
        } else {
            rollbackResult = mCtx.fileManager->RollbackSoftwarePackage(serviceName);
        }
        if (!rollbackResult.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to rollback software package: " << rollbackResult.msg;
        }

        // 2. 重新加载配置以匹配回滚后的文件
        auto reloadResult = mCtx.configLoader->ReloadService(serviceName);
        if (!reloadResult.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to reload service config after rollback: " << reloadResult.msg;
        }

        // 3. 重新生成 systemd 服务文件
        auto genResult = mServiceManager.GenerateAndCreateServiceFile(serviceName);
        if (!genResult.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to regenerate service file after rollback: " << genResult.msg;
        }

        // 4. 回滚数据库（若 db_backup 存在）
        auto dbRollback = mDbService.RollbackServiceDatabase(serviceName);
        if (!dbRollback.IsDefalutSuccess()) {
            SLOG_WARN << "Database rollback skipped or failed: " << dbRollback.msg;
        }

        // 5. 如果升级前服务在运行，尝试重新启动
        if (restartIfWasRunning) {
            auto startResult = mServiceManager.StartService(serviceName);
            if (!startResult.IsDefalutSuccess()) {
                SLOG_ERROR << "Failed to restart service after rollback: " << startResult.msg;
            }
        }

        // 6. 回滚完成后清理备份
        auto cleanResult = mCtx.fileManager->CleanBackup(serviceName);
        if (!cleanResult.IsDefalutSuccess()) {
            SLOG_WARN << "Failed to clean backup after rollback: " << cleanResult.msg;
        }

        SLOG_INFO << "Upgrade rollback completed for service: " << serviceName;
    }

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
        } else if (mServiceManager.IsTarPackage(srcPath)) {
            // tar 包：解压到临时目录
            tempDir = utils::GenerateTempDir(mCtx.fileManager->GetCurDirConfig().tempDir);
            auto extractRet = mServiceManager.ExtractSoftwareTar(srcPath, tempDir);
            if (!extractRet.IsDefalutSuccess()) {
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
                if (fs::is_directory(entry.path()) || mServiceManager.IsTarPackage(name)) {
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
            if (name.find(serviceName) == 0) {
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

    // --- 升级辅助 ---

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg UpgradeService::PrepareUpgradeSource(const std::string &serviceName, const std::string &softwareTarPath,
                                                   const std::string &tempDir) {
        if (mServiceManager.IsTarPackage(softwareTarPath)) {
            return mServiceManager.ExtractSoftwareTar(softwareTarPath, tempDir, serviceName);
        }
        if (fs::is_directory(softwareTarPath)) {
            // 已解压目录：解析服务名后移动到临时目录统一处理
            // 支持两种目录结构：dir/service.yaml 或 dir/<serviceName>/service.yaml
            std::string yamlPath = utils::JoinPath(softwareTarPath, DefaultServiceName);
            if (!fs::exists(yamlPath)) {
                yamlPath = utils::JoinPath(softwareTarPath, serviceName, DefaultServiceName);
            }
            auto configServiceName = mServiceManager.ResolveServiceName(yamlPath);
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
    ResultMsg UpgradeService::UpdateServiceWithDetail(const std::string &serviceName, const std::string &sourceDir,
                                                      const std::string &upDetailPath, bool wasRunning) {
        SLOG_INFO << "Updating service with detail: " << serviceName;

        // 1. 解析 up_detail.yaml
        UpgradeDetail detail;
        auto result = mCtx.fileManager->ParseUpgradeDetail(upDetailPath, detail);
        if (!result.IsDefalutSuccess()) {
            return MakeError("Failed to parse up_detail.yaml: " + result.msg);
        }

        // 2. 备份数据库（若依赖 mariadb 且有 initDB_sql_dir）
        auto dbBackupResult = mDbService.BackupServiceDatabase(serviceName);
        if (!dbBackupResult.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to backup database: " << dbBackupResult.msg;
            DoRollbackUpgrade(serviceName, wasRunning, true);
            return MakeError("Failed to backup database: " + dbBackupResult.msg);
        }

        // 4. 细粒度文件备份
        result = mCtx.fileManager->BackupForFineGrainedUpgrade(serviceName, detail);
        if (!result.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to backup for fine-grained upgrade: " << result.msg;
            DoRollbackUpgrade(serviceName, wasRunning, true);
            return MakeError("Failed to backup for fine-grained upgrade: " + result.msg);
        }

        // 5. 执行细粒度文件升级
        result = mCtx.fileManager->ApplyFineGrainedUpgrade(serviceName, sourceDir, detail);
        if (!result.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to apply fine-grained upgrade: " << result.msg;
            DoRollbackUpgrade(serviceName, wasRunning, true);
            return MakeError("Failed to apply fine-grained upgrade: " + result.msg);
        }

        // 6. 重新加载配置（service.yaml 可能已通过 replace 更新）
        std::string installedServiceDir = mCtx.fileManager->GetServiceWDir(serviceName);
        result = mCtx.configLoader->UpgradeService(installedServiceDir);
        if (!result.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to upgrade service config: " << result.msg;
            DoRollbackUpgrade(serviceName, wasRunning, true);
            return MakeError("Failed to upgrade service config: " + result.msg);
        }

        // 7. 重新生成 systemd 服务文件
        result = mServiceManager.GenerateAndCreateServiceFile(serviceName);
        if (!result.IsDefalutSuccess()) {
            DoRollbackUpgrade(serviceName, wasRunning, true);
            return result;
        }

        // 8. 执行升级 SQL 脚本（若 initDB_sql_dir 存在）
        auto sqlResult = mDbService.ExecuteUpgradeScripts(serviceName);
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
    ResultMsg UpgradeService::UpdateServiceDefault(const std::string &serviceName, const std::string &parentDir,
                                                   bool wasRunning) {
        SLOG_INFO << "Updating service with default (full) strategy: " << serviceName;

        // parentDir 为临时目录（包含 <serviceName>/ 子目录），UpgradeSoftwarePackage 内部会执行
        // JoinPath(softwareDir, serviceName) 定位实际软件包，与 InstallSoftwarePackage 路径约定一致
        auto result = mCtx.fileManager->UpgradeSoftwarePackage(serviceName, parentDir);
        if (!result.IsDefalutSuccess()) {
            DoRollbackUpgrade(serviceName, wasRunning, false);
            return MakeError("Failed to upgrade software package: " + result.msg);
        }

        // 使用 ConfigLoader::UpgradeService 更新配置
        std::string installedServiceDir = mCtx.fileManager->GetServiceWDir(serviceName);
        result = mCtx.configLoader->UpgradeService(installedServiceDir);
        if (!result.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to upgrade service config: " << result.msg;
            DoRollbackUpgrade(serviceName, wasRunning, false);
            return MakeError("Failed to upgrade service config: " + result.msg);
        }

        result = mServiceManager.GenerateAndCreateServiceFile(serviceName);
        if (!result.IsDefalutSuccess()) {
            DoRollbackUpgrade(serviceName, wasRunning, false);
            return result;
        }

        // 全量升级也需要备份数据库（若依赖 mariadb 且有 initDB_sql_dir）
        auto dbBackupResult = mDbService.BackupServiceDatabase(serviceName);
        if (!dbBackupResult.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to backup database: " << dbBackupResult.msg;
            DoRollbackUpgrade(serviceName, wasRunning, false);
            return MakeError("Failed to backup database: " + dbBackupResult.msg);
        }

        // 执行升级 SQL 脚本（若 initDB_sql_dir 存在），失败则整库回退
        auto sqlResult = mDbService.ExecuteUpgradeScripts(serviceName);
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

    // --- 内部预置升级包辅助 ---

    ResultMsg UpgradeService::FindInternalUpgradePackage(const std::string &serviceName) {
        auto* svc = mCtx.configLoader->GetServiceByName(serviceName);
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
    std::string UpgradeService::ReadVersionFromPackage(const std::string &serviceName, const std::string &packagePath) {
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
        std::string tempDir = utils::GenerateTempDir(mCtx.fileManager->GetCurDirConfig().tempDir);
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

    // --- 一体化升级编排 ---

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
            if (!writeRet.IsDefalutSuccess()) {
                SLOG_WARN << "Failed to write upgrade result file: " << writeRet.msg;
            }
        };

        // 辅助：恢复 nginx 正常配置（流程失败时调用）
        auto restoreNginx = [&]() {
            SLOG_INFO << "Restoring nginx to normal mode";
            auto ret = mNginxManager.ResetNginx(NginxResetMode::NORMAL);
            if (!ret.IsDefalutSuccess()) {
                SLOG_ERROR << "Failed to restore nginx: " << ret.msg;
            }
        };

        // 1. 进入 nginx 等待页面（所有路由返回404，避免升级期间访问到不一致状态）
        SLOG_INFO << "Step 1: Set nginx to waiting mode";
        auto waitRet = mNginxManager.ResetNginx(NginxResetMode::WAIT);
        if (!waitRet.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to set nginx waiting mode: " << waitRet.msg;
            // nginx 未进入等待态，直接返回，不继续后续流程
            return waitRet;
        }

        // 2. 查找升级素材（服务包/model/nginx）
        //    使用 RAII 确保临时解压目录在所有退出路径上被清理
        SLOG_INFO << "Step 2: Find upgrade artifacts";
        UpgradeArtifacts artifacts;
        auto findRet = FindUpgradeArtifacts(serviceName, tarDir, artifacts);
        if (!findRet.IsDefalutSuccess()) {
            SLOG_ERROR << "Find upgrade artifacts failed: " << findRet.msg;
            restoreNginx();
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
            if (!modelRet.IsDefalutSuccess()) {
                SLOG_ERROR << "Add model failed: " << modelRet.msg;
                restoreNginx();
                writeResult(false, modelRet.msg);
                return modelRet;
            }
            SLOG_INFO << "Model added successfully: " << modelInstalledName << " (backup retained)";
        } else {
            SLOG_INFO << "Step 3: No model artifact, skip";
        }

        // 4. 若有服务包：执行服务升级，失败则回退模型
        ResultMsg result = MakeSuccess();
        if (!artifacts.servicePackage.empty()) {
            SLOG_INFO << "Step 4: Upgrade service from package: " << artifacts.servicePackage;
            // 解析新版本号（用于结果记录，失败不影响升级流程）
            newVersion = ReadVersionFromPackage(serviceName, artifacts.servicePackage);
            SLOG_INFO << "New version from package: " << (newVersion.empty() ? "<unknown>" : newVersion);

            result = UpdateService(serviceName, artifacts.servicePackage);
            if (result.IsDefalutSuccess()) {
                // 确认升级完成，清理旧版本备份
                auto cleanRet = CleanUpgradeBackup(serviceName);
                if (!cleanRet.IsDefalutSuccess()) {
                    SLOG_WARN << "Failed to clean upgrade backup: " << cleanRet.msg;
                }
                SLOG_INFO << "Service upgraded successfully";
            } else {
                SLOG_ERROR << "Upgrade service failed: " << result.msg << ", rolling back model if any";
                // 回退模型（若已安装）
                if (!modelInstalledName.empty()) {
                    auto rollbackRet = mModelManager.RollbackModel(modelInstalledName);
                    if (!rollbackRet.IsDefalutSuccess()) {
                        SLOG_ERROR << "Rollback model failed: " << rollbackRet.msg;
                    }
                }
                restoreNginx();
                writeResult(false, result.msg);
                return result;
            }
        } else {
            SLOG_INFO << "Step 4: No service package, skip service upgrade";
        }

        // 5. 成功收尾：清理模型备份，更新或恢复 nginx 配置
        SLOG_INFO << "Step 5: Finalize - clean model backup, update nginx";
        if (!modelInstalledName.empty()) {
            auto cleanRet = mModelManager.CleanModelBackup(modelInstalledName);
            if (!cleanRet.IsDefalutSuccess()) {
                SLOG_WARN << "Failed to clean model backup: " << cleanRet.msg;
            }
        }

        if (!artifacts.nginxDir.empty()) {
            // 有 nginx 素材：使用第一个 nginx 前缀目录更新配置
            // 注意：此处失败不回滚已升级的服务/模型（核心升级已成功），仅恢复 nginx 旧配置并返回警告
            SLOG_INFO << "Updating nginx config from: " << artifacts.nginxDir;
            auto nginxRet = mNginxManager.InitNginx(artifacts.nginxDir);
            if (!nginxRet.IsDefalutSuccess()) {
                SLOG_ERROR << "InitNginx failed: " << nginxRet.msg << ", fallback to reset_nginx -n";
                restoreNginx();
                std::string warnMsg = "Service upgraded successfully, but nginx config update failed: " + nginxRet.msg
                                      + " (nginx restored to previous config)";
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

        writeResult(true, "");
        SLOG_INFO << "Integrated upgrade completed successfully for service: " << serviceName;
        return MakeSuccess();
    }

}  // namespace qifeng::scm
