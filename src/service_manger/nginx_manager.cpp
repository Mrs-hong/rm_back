/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "service_manger/nginx_manager.h"

#include "common/utils.h"
#include "common/utils/file.h"
#include "qifeng_framework/common/logger.h"
#include "service_manger/file_manager.h"
#include "service_manger/service_context.h"
#include "service_manger/service_manager.h"
#include "service_tool/tool_nginx.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace qifeng::scm {

    NginxManager::NginxManager(const ServiceContext &ctx, ServiceManager &serviceManager)
        : mCtx(ctx), mServiceManager(serviceManager) {
    }

    NginxManager::~NginxManager() = default;

    // --- 独立 nginx 配置管理 ---

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg NginxManager::InitNginx(const std::string &dirPath) {
        SLOG_INFO << "InitNginx from: " << dirPath;

        // 解析源路径：tar 包则解压，目录则直接使用
        std::string nginxSrcDir;
        std::string tempDir;

        if (mServiceManager.IsTarPackage(dirPath)) {
            // tar 包：解压到临时目录（不指定子目录名，由 FileManager 内部查找 nginx/frontend 子目录）
            tempDir = utils::GenerateTempDir(mCtx.fileManager->GetCurDirConfig().tempDir);
            auto extractResult = mServiceManager.ExtractSoftwareTar(dirPath, tempDir);
            if (!extractResult.IsDefalutSuccess()) {
                mServiceManager.CleanupTempDirectory(tempDir);
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
        auto result = mCtx.fileManager->InitNginx(nginxSrcDir);

        // 清理临时目录
        if (!tempDir.empty()) {
            mServiceManager.CleanupTempDirectory(tempDir);
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
            auto rollbackResult = mCtx.fileManager->ResetNginx();
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

    ResultMsg NginxManager::ResetNginx(NginxResetMode mode) {
        SLOG_INFO << "ResetNginx mode=" << static_cast<int>(mode);

        // 根据 mode 分发到不同的 FileManager 操作
        ResultMsg result;
        switch (mode) {
            case NginxResetMode::WAIT:
                result = mCtx.fileManager->SetNginxWaiting();
                break;
            case NginxResetMode::NORMAL:
                result = mCtx.fileManager->SetNginxNormal();
                break;
            case NginxResetMode::BACK:
                result = mCtx.fileManager->ResetNginx();
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

}  // namespace qifeng::scm
