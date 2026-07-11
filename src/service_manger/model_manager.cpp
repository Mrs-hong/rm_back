/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "service_manger/model_manager.h"

#include "common/config.h"
#include "common/scmd_def.h"
#include "common/types.h"
#include "common/utils.h"
#include "common/utils/file.h"
#include "common/utils/string.h"
#include "qifeng_framework/common/logger.h"
#include "service_manger/file_manager.h"
#include "service_manger/service_context.h"
#include "service_manger/service_manager.h"

#include <chrono>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

namespace qifeng::scm {

    ModelManager::ModelManager(const ServiceContext &ctx, ServiceManager &serviceManager)
        : mCtx(ctx), mServiceManager(serviceManager) {
        SLOG_INFO << "ModelManager initialized";
    }

    ModelManager::~ModelManager() {
        SLOG_INFO << "ModelManager destroyed";
    }

    // --- 模型管理 ---

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg ModelManager::AddModel(const std::string &srcPath) {
        // 1. 前置校验
        if (srcPath.empty()) {
            return MakeError("Model source path is empty");
        }
        const auto &configInfo = mCtx.configLoader->GetConfigInfo();
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
        if (mServiceManager.IsTarPackage(srcPath)) {
            tempDir = utils::GenerateTempDir(mCtx.fileManager->GetCurDirConfig().tempDir);
            auto extractRet = mServiceManager.ExtractSoftwareTar(srcPath, tempDir);
            if (!extractRet.IsDefalutSuccess()) {
                mServiceManager.CleanupTempDirectory(tempDir);
                return MakeError("Failed to extract model tar: " + extractRet.msg);
            }
            // 取解压目录下唯一的顶层条目名（要求是目录）
            modelName = utils::GetSingleTopLevelEntryName(tempDir);
            if (modelName.empty()) {
                mServiceManager.CleanupTempDirectory(tempDir);
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
            mServiceManager.CleanupTempDirectory(tempDir);
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
            mServiceManager.CleanupTempDirectory(tempDir);
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
                mServiceManager.CleanupTempDirectory(tempDir);
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
            mServiceManager.CleanupTempDirectory(tempDir);
            return MakeError("Failed to copy model to model_dir: " + moveRet.msg);
        }
        mServiceManager.CleanupTempDirectory(tempDir);

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

    std::vector<std::string> ModelManager::GetModelDependentServices(const std::string &excludeService) {
        std::vector<std::string> result;
        auto allServices = mCtx.configLoader->GetAllServices();
        for (const auto &svc : allServices) {
            if (svc.needModel && svc.serviceName != excludeService) {
                result.push_back(svc.serviceName);
            }
        }
        return result;
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg ModelManager::AddModelWithBackupRetained(const std::string &srcPath, const std::string &excludeService,
                                                        std::string &modelName) {
        // 与 AddModel 流程一致，但：1) 排除 excludeService  2) 不清理 .back 备份
        if (srcPath.empty()) {
            return MakeError("Model source path is empty");
        }
        const auto &configInfo = mCtx.configLoader->GetConfigInfo();
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
        if (mServiceManager.IsTarPackage(srcPath)) {
            tempDir = utils::GenerateTempDir(mCtx.fileManager->GetCurDirConfig().tempDir);
            auto extractRet = mServiceManager.ExtractSoftwareTar(srcPath, tempDir);
            if (!extractRet.IsDefalutSuccess()) {
                mServiceManager.CleanupTempDirectory(tempDir);
                return MakeError("Failed to extract model tar: " + extractRet.msg);
            }
            modelName = utils::GetSingleTopLevelEntryName(tempDir);
            if (modelName.empty()) {
                mServiceManager.CleanupTempDirectory(tempDir);
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
            mServiceManager.CleanupTempDirectory(tempDir);
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
            mServiceManager.CleanupTempDirectory(tempDir);
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
                mServiceManager.CleanupTempDirectory(tempDir);
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
            mServiceManager.CleanupTempDirectory(tempDir);
            return MakeError("Failed to copy model to model_dir: " + moveRet.msg);
        }
        mServiceManager.CleanupTempDirectory(tempDir);

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

    ResultMsg ModelManager::CleanModelBackup(const std::string &modelName) {
        if (modelName.empty()) {
            return MakeError("Model name is empty");
        }
        const auto &configInfo = mCtx.configLoader->GetConfigInfo();
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

    ResultMsg ModelManager::RollbackModel(const std::string &modelName) {
        if (modelName.empty()) {
            return MakeError("Model name is empty");
        }
        const auto &configInfo = mCtx.configLoader->GetConfigInfo();
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

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg ModelManager::ClearModel(const std::string &modelName) {
        // 1. 前置校验
        auto nameRet = ValidateModelName(modelName);
        if (!nameRet.IsDefalutSuccess()) {
            return nameRet;
        }
        const auto &configInfo = mCtx.configLoader->GetConfigInfo();
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

    std::vector<std::string> ModelManager::GetModelDependentServices() {
        std::vector<std::string> result;
        auto allServices = mCtx.configLoader->GetAllServices();
        for (const auto &svc : allServices) {
            if (svc.needModel) {
                result.push_back(svc.serviceName);
            }
        }
        return result;
    }

    ResultMsg ModelManager::StopServicesWithStateRecord(const std::vector<std::string> &serviceNames,
                                                         std::map<std::string, bool> &preStates) {
        preStates.clear();
        for (const auto &name : serviceNames) {
            // 记录停止前是否在运行
            bool wasRunning = mServiceManager.IsServiceActive(name);
            preStates[name] = wasRunning;
            if (wasRunning) {
                auto stopRet = mServiceManager.StopService(name);
                if (!stopRet.IsDefalutSuccess()) {
                    // 停止失败：已停止的服务保持原状态记录，直接返回错误
                    return MakeError("Failed to stop service " + name + ": " + stopRet.msg);
                }
            }
        }
        return MakeSuccess();
    }

    ResultMsg ModelManager::RestoreServicesByState(const std::map<std::string, bool> &preStates) {
        // 按停止前状态恢复：原本运行的启动，原本停止的保持停止
        // 失败仅告警不中断，确保回退流程能继续执行
        for (const auto &kv : preStates) {
            if (kv.second) {
                // 原本在运行，尝试启动
                auto startRet = mServiceManager.StartService(kv.first);
                if (!startRet.IsDefalutSuccess()) {
                    SLOG_WARN << "Failed to restore service " << kv.first << " to running state: " << startRet.msg;
                }
            }
        }
        return MakeSuccess();
    }

    ResultMsg ModelManager::StartServicesAndWaitRunning(const std::vector<std::string> &serviceNames) {
        // 逐个启动服务并按各自的 keep_alive_time_sec 验证持续运行
        // 每个服务独立验证：启动后等待 keepAliveTimeSec 秒，再检查是否仍活跃
        for (const auto &name : serviceNames) {
            auto startRet = mServiceManager.StartService(name);
            if (!startRet.IsDefalutSuccess()) {
                return MakeError("Failed to start service " + name + ": " + startRet.msg);
            }

            // 查询服务的 keep_alive_time_sec：0 表示跳过验证
            auto* svc = mCtx.configLoader->GetServiceByName(name);
            uint32_t keepSec = svc ? svc->keepAliveTimeSec : 0;
            if (keepSec == 0) {
                continue;
            }

            // 等待验证时长，确保服务持续运行
            std::this_thread::sleep_for(std::chrono::seconds(keepSec));

            // 检查该服务是否仍处于活跃状态
            if (!mServiceManager.IsServiceActive(name)) {
                return MakeError("Service " + name + " is not active after " + std::to_string(keepSec) + "s");
            }
        }
        return MakeSuccess();
    }

    ResultMsg ModelManager::ValidateModelName(const std::string &modelName) const {
        if (modelName.empty()) {
            return MakeError("Model name is empty");
        }
        // 禁止使用 .back 结尾的模型名，避免与备份命名冲突
        if (modelName.size() >= 5 && modelName.compare(modelName.size() - 5, 5, ".back") == 0) {
            return MakeError("Model name must not end with '.back': " + modelName);
        }
        return MakeSuccess();
    }

}  // namespace qifeng::scm
