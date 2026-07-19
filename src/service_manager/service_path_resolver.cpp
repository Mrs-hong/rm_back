/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 *
 * 本文件承载 FileManager 类的"路径解析"相关方法实现。
 * 拆分目的：file_manager.cpp 体量过大，按职责将路径解析逻辑独立成文件，
 *           降低单文件复杂度，便于维护。所有方法声明仍在 file_manager.h 中，
 *           未对类接口做任何修改。
 */
#include "service_manager/file_manager.h"

#include "common/utils/path.h"

namespace qifeng::scm {

    // --- 查询接口------

    std::string FileManager::GetServiceWDir(const std::string &serviceName) const {
        return GetServiceDir(serviceName);
    }

    std::string FileManager::GetServiceConfigPath(const std::string &serviceName) const {
        return utils::JoinPath(GetServiceDir(serviceName), ServiceYamlName);
    }

    const FileDirInfo &FileManager::GetCurDirConfig() const {
        return mCurDirConfig;
    }

    // --- 私有路径辅助方法------

    std::string FileManager::GetServiceInitDir() const {
        return utils::JoinPath(mCurDirConfig.serviceDir, InitDirName);
    }

    std::string FileManager::GetServiceInitFilePath(const std::string &serviceName) const {
        return utils::JoinPath(GetServiceInitDir(), ServiceFilePrefix + serviceName + ServiceFileSuffix);
    }

    std::string FileManager::GetServiceDir(const std::string &serviceName) const {
        return utils::JoinPath(mCurDirConfig.serviceDir, serviceName);
    }

    std::string FileManager::GetBackupDir(const std::string &serviceName) const {
        return utils::JoinPath(mCurDirConfig.backupDir, serviceName);
    }

    std::string FileManager::GetDataDir(const std::string &serviceName) const {
        return utils::JoinPath(mCurDirConfig.dataDir, serviceName);
    }

    std::string FileManager::GetFrontendDir(const std::string &serviceName) const {
        return utils::JoinPath(GetServiceDir(serviceName), "nginx");
    }

    // --- 独立 nginx 配置管理 ---

    std::string FileManager::GetNginxDir() const {
        return utils::JoinPath(mCurDirConfig.serviceDir, "nginx");
    }

}  // namespace qifeng::scm
