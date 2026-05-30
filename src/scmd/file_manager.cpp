/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#include "service_manger/file_manager.h"

#include "common/utils.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

namespace qifeng::scm {

    bool FileDirInfo::operator==(const FileDirInfo &other) const {
        return configDir == other.configDir && serviceDir == other.serviceDir && dataDir == other.dataDir &&
               backupDir == other.backupDir && logsDir == other.logsDir;
    }

    FileManager::FileManager(FileDirInfo &&dirConfig) : mCurDirConfig(std::move(dirConfig)) {
    }

    ResultMsg FileManager::InitFileDir() {
        // 创建 .config 目录
        auto ret = utils::CreateDirectory(mCurDirConfig.configDir);
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Failed to create config dir: " + ret.msg);
        }

        // 创建 .service 目录
        ret = utils::CreateDirectory(mCurDirConfig.serviceDir);
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Failed to create service dir: " + ret.msg);
        }

        // 创建 .service/.init 子目录
        ret = utils::CreateDirectory(GetServiceInitDir());
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Failed to create service init dir: " + ret.msg);
        }

        // 创建 .data 目录
        ret = utils::CreateDirectory(mCurDirConfig.dataDir);
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Failed to create data dir: " + ret.msg);
        }

        // 创建 .backup 目录
        ret = utils::CreateDirectory(mCurDirConfig.backupDir);
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Failed to create backup dir: " + ret.msg);
        }

        // 创建 .logs 目录
        ret = utils::CreateDirectory(mCurDirConfig.logsDir);
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Failed to create logs dir: " + ret.msg);
        }

        return MakeSuccess();
    }

    ResultMsg FileManager::MigrateFileDir(const FileDirInfo &newDirConfig) {
        // 验证新配置与旧配置不同
        if (mCurDirConfig == newDirConfig) {
            return MakeWarning("New directory config is the same as current config");
        }

        // 移动各目录到新位置
        if (fs::exists(mCurDirConfig.configDir)) {
            auto ret = utils::MoveDirectory(mCurDirConfig.configDir, newDirConfig.configDir);
            if (!ret.IsDefalutSuccess()) {
                return MakeError("Failed to migrate config dir: " + ret.msg);
            }
        }

        if (fs::exists(mCurDirConfig.serviceDir)) {
            auto ret = utils::MoveDirectory(mCurDirConfig.serviceDir, newDirConfig.serviceDir);
            if (!ret.IsDefalutSuccess()) {
                return MakeError("Failed to migrate service dir: " + ret.msg);
            }
        }

        if (fs::exists(mCurDirConfig.dataDir)) {
            auto ret = utils::MoveDirectory(mCurDirConfig.dataDir, newDirConfig.dataDir);
            if (!ret.IsDefalutSuccess()) {
                return MakeError("Failed to migrate data dir: " + ret.msg);
            }
        }

        if (fs::exists(mCurDirConfig.backupDir)) {
            auto ret = utils::MoveDirectory(mCurDirConfig.backupDir, newDirConfig.backupDir);
            if (!ret.IsDefalutSuccess()) {
                return MakeError("Failed to migrate backup dir: " + ret.msg);
            }
        }

        if (fs::exists(mCurDirConfig.logsDir)) {
            auto ret = utils::MoveDirectory(mCurDirConfig.logsDir, newDirConfig.logsDir);
            if (!ret.IsDefalutSuccess()) {
                return MakeError("Failed to migrate logs dir: " + ret.msg);
            }
        }

        // 更新目录配置
        mCurDirConfig = newDirConfig;

        // 确保 .init 子目录存在
        auto ret = utils::CreateDirectory(GetServiceInitDir());
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Failed to create init dir after migration: " + ret.msg);
        }

        return MakeSuccess();
    }

    ResultMsg FileManager::DeleteFileDir() {
        // 先删除所有 systemd 软链接
        auto serviceFiles = GetAllServiceFilesList();
        for (const auto &fileName : serviceFiles) {
            utils::DeleteSymbolicLink(std::string(SystemdUnitDir) + "/" + fileName);
        }

        // 删除各目录
        utils::ForceDeleteDirectory(mCurDirConfig.serviceDir);
        utils::ForceDeleteDirectory(mCurDirConfig.dataDir);
        utils::ForceDeleteDirectory(mCurDirConfig.backupDir);
        utils::ForceDeleteDirectory(mCurDirConfig.logsDir);

        return MakeSuccess();
    }

    // --- 软件包管理------

    ResultMsg FileManager::InstallSoftwarePackage(const std::string &serviceName, const std::string &softwareDir) {
        // 检查服务是否已安装
        std::string serviceDir = GetServiceDir(serviceName);
        if (fs::exists(serviceDir)) {
            return MakeError("Service already installed: " + serviceName);
        }

        // 验证软件包完整性
        auto ret = VerifySoftwarePackage(softwareDir);
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Software package verification failed: " + ret.msg);
        }

        // 复制软件包到服务目录
        auto copyRet = utils::CopyDirectory(softwareDir, serviceDir);
        if (!copyRet.IsDefalutSuccess()) {
            // 失败时清理已创建的目录
            utils::ForceDeleteDirectory(serviceDir);
            return MakeError("Failed to copy software package: " + copyRet.msg);
        }

        // 验证安装后结构
        std::string installedYaml = serviceDir + "/" + ServiceYamlName;
        if (!fs::exists(installedYaml)) {
            utils::ForceDeleteDirectory(serviceDir);
            return MakeError("Installed package missing service.yaml");
        }

        return MakeSuccess();
    }

    std::string FileManager::GetServiceWDir(const std::string &serviceName) const {
        return GetServiceDir(serviceName);
    }

    std::vector<std::string> FileManager::GetAllServicesList() {
        std::vector<std::string> services;
        std::string serviceRoot = mCurDirConfig.serviceDir;

        if (!fs::exists(serviceRoot)) {
            return services;
        }

        std::error_code ec;
        for (const auto &entry : fs::directory_iterator(serviceRoot, ec)) {
            if (!entry.is_directory()) {
                continue;
            }

            std::string dirName = entry.path().filename().string();

            // 排除 .init 目录
            if (dirName == InitDirName) {
                continue;
            }

            // 检查是否包含 service.yaml
            std::string yamlPath = entry.path().string() + "/" + ServiceYamlName;
            if (fs::exists(yamlPath)) {
                services.push_back(dirName);
            }
        }

        return services;
    }

    ResultMsg FileManager::UpgradeSoftwarePackage(const std::string &serviceName, const std::string &softwareDir) {
        std::string serviceDir = GetServiceDir(serviceName);

        // 检查服务已安装
        if (!fs::exists(serviceDir)) {
            return MakeError("Service not installed: " + serviceName);
        }

        // 备份旧版本（排除数据目录）
        auto ret = BackupOldVersion(serviceName);
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Failed to backup old version: " + ret.msg);
        }

        // 验证新软件包
        ret = VerifySoftwarePackage(softwareDir);
        if (!ret.IsDefalutSuccess()) {
            return MakeError("New software package verification failed: " + ret.msg);
        }

        // 读取旧服务的数据目录名
        std::string dataDirName = ReadServiceDataDirName(serviceName);

        // 删除旧服务文件（保留数据目录）
        ret = ClearDirectoryContentsExclude(serviceDir, dataDirName);
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Failed to clear old service files: " + ret.msg);
        }

        // 复制新软件包到服务目录
        auto copyRet = utils::CopyDirectory(softwareDir, serviceDir);
        if (!copyRet.IsDefalutSuccess()) {
            // 复制失败，尝试回滚
            RollbackSoftwarePackage(serviceName);
            return MakeError("Failed to copy new software package: " + copyRet.msg);
        }

        // 验证安装后结构
        std::string installedYaml = serviceDir + "/" + ServiceYamlName;
        if (!fs::exists(installedYaml)) {
            RollbackSoftwarePackage(serviceName);
            return MakeError("Upgraded package missing service.yaml");
        }

        return MakeSuccess();
    }

    ResultMsg FileManager::RemoveSoftwarePackage(const std::string &serviceName) {
        std::string serviceDir = GetServiceDir(serviceName);

        // 检查服务已安装
        if (!fs::exists(serviceDir)) {
            return MakeError("Service not installed: " + serviceName);
        }

        // 备份旧版本（安全措施，防止误删）
        BackupOldVersion(serviceName);

        // 删除 .init 中的 service 文件
        DeleteServiceFile(serviceName);

        // 删除 systemd 软链接
        DeleteServiceSymlink(serviceName);

        // 删除服务目录
        auto ret = utils::ForceDeleteDirectory(serviceDir);
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Failed to remove service directory: " + ret.msg);
        }

        return MakeSuccess();
    }

    ResultMsg FileManager::RollbackSoftwarePackage(const std::string &serviceName) {
        std::string backupDir = GetBackupDir(serviceName);
        std::string serviceDir = GetServiceDir(serviceName);

        // 检查备份目录存在
        if (!fs::exists(backupDir)) {
            return MakeError("Backup not found for service: " + serviceName);
        }

        // 读取当前服务的数据目录名
        std::string dataDirName = ReadServiceDataDirName(serviceName);

        // 删除当前服务文件（保留数据目录）
        auto ret = ClearDirectoryContentsExclude(serviceDir, dataDirName);
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Failed to clear current service files during rollback: " + ret.msg);
        }

        // 将备份内容复制回服务目录
        auto copyRet = utils::CopyDirectory(backupDir, serviceDir);
        if (!copyRet.IsDefalutSuccess()) {
            return MakeError("Failed to restore from backup: " + copyRet.msg);
        }

        return MakeSuccess();
    }

    ResultMsg FileManager::CleanBackup(const std::string &serviceName) {
        std::string backupDir = GetBackupDir(serviceName);

        if (!fs::exists(backupDir)) {
            return MakeWarning("Backup not found for service: " + serviceName);
        }

        return utils::ForceDeleteDirectory(backupDir);
    }

    // --- service文件管理------

    ResultMsg FileManager::CreateServiceFile(const std::string &fileData, const std::string &serviceName) {
        // 确保 .init 目录存在
        auto ret = utils::CreateDirectory(GetServiceInitDir());
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Failed to create init directory: " + ret.msg);
        }

        std::string filePath = GetServiceInitFilePath(serviceName);

        // 检查文件是否已存在
        if (fs::exists(filePath)) {
            return MakeError("Service file already exists: " + filePath);
        }

        // 写入文件
        std::ofstream ofs(filePath);
        if (!ofs.is_open()) {
            return MakeError("Failed to create service file: " + filePath);
        }
        ofs << fileData;
        ofs.close();

        return MakeSuccess();
    }

    std::vector<std::string> FileManager::GetAllServiceFilesList() {
        std::vector<std::string> files;
        std::string initDir = GetServiceInitDir();

        if (!fs::exists(initDir)) {
            return files;
        }

        std::error_code ec;
        for (const auto &entry : fs::directory_iterator(initDir, ec)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            std::string filename = entry.path().filename().string();

            // 只返回 scmd_*.service 格式的文件
            if (filename.find(ServiceFilePrefix) == 0 && filename.size() >= strlen(ServiceFileSuffix) &&
                filename.substr(filename.size() - strlen(ServiceFileSuffix)) == ServiceFileSuffix) {
                files.push_back(filename);
            }
        }

        return files;
    }

    ResultMsg FileManager::FreshServiceFile(const std::string &newFileData, const std::string &serviceName) {
        std::string filePath = GetServiceInitFilePath(serviceName);

        if (!fs::exists(filePath)) {
            return MakeError("Service file not found: " + filePath);
        }

        // 覆盖写入新内容
        std::ofstream ofs(filePath, std::ios::trunc);
        if (!ofs.is_open()) {
            return MakeError("Failed to open service file for writing: " + filePath);
        }
        ofs << newFileData;
        ofs.close();

        return MakeSuccess();
    }

    std::string FileManager::GetServiceFileContent(const std::string &serviceName) {
        std::string filePath = GetServiceInitFilePath(serviceName);

        if (!fs::exists(filePath)) {
            return "";
        }

        std::ifstream ifs(filePath);
        if (!ifs.is_open()) {
            return "";
        }

        std::ostringstream oss;
        oss << ifs.rdbuf();
        return oss.str();
    }

    ResultMsg FileManager::DeleteServiceFile(const std::string &serviceName) {
        std::string filePath = GetServiceInitFilePath(serviceName);

        if (!fs::exists(filePath)) {
            return MakeWarning("Service file not found: " + filePath);
        }

        std::error_code ec;
        fs::remove(filePath, ec);
        if (ec) {
            return MakeError("Failed to delete service file: " + filePath + ", " + ec.message());
        }

        return MakeSuccess();
    }

    // --- 软链接管理------

    ResultMsg FileManager::CreateServiceSymlink(const std::string &serviceName) {
        std::string initFilePath = GetServiceInitFilePath(serviceName);

        // 检查 .init 文件存在
        if (!fs::exists(initFilePath)) {
            return MakeError("Service init file not found: " + initFilePath);
        }

        // 构造软链接路径和目标路径
        std::string linkPath = std::string(SystemdUnitDir) + "/" + ServiceFilePrefix + serviceName + ServiceFileSuffix;
        std::string targetPath = utils::GetAbsolutePath(initFilePath);

        return utils::CreateSymbolicLink(targetPath, linkPath);
    }

    ResultMsg FileManager::DeleteServiceSymlink(const std::string &serviceName) {
        std::string linkPath = std::string(SystemdUnitDir) + "/" + ServiceFilePrefix + serviceName + ServiceFileSuffix;

        return utils::DeleteSymbolicLink(linkPath);
    }

    // --- 数据目录管理------

    ResultMsg FileManager::MigrateServiceData(const std::string &serviceName, const std::string &dataSubDir) {
        std::string serviceDir = GetServiceDir(serviceName);
        std::string srcDataPath = serviceDir + "/" + dataSubDir;

        // 检查源数据目录存在
        if (!fs::exists(srcDataPath)) {
            return MakeError("Source data directory does not exist: " + srcDataPath);
        }

        // 创建 .data/xxxx/ 目标目录
        std::string dataDir = GetDataDir(serviceName);
        auto ret = utils::CreateDirectory(dataDir);
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Failed to create data directory: " + ret.msg);
        }

        // 移动数据目录到 .data/xxxx/dataSubDir
        std::string dstDataPath = dataDir + "/" + dataSubDir;

        // 确保目标父目录存在
        fs::path dstParentPath = fs::path(dstDataPath).parent_path();
        if (!dstParentPath.empty()) {
            ret = utils::CreateDirectory(dstParentPath.string());
            if (!ret.IsDefalutSuccess()) {
                return MakeError("Failed to create data subdirectory: " + ret.msg);
            }
        }

        ret = utils::MoveDirectory(srcDataPath, dstDataPath);
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Failed to migrate data directory: " + ret.msg);
        }

        // 在原位置创建软链接指向新位置，保持兼容性
        std::string absDstPath = utils::GetAbsolutePath(dstDataPath);
        ret = utils::CreateSymbolicLink(absDstPath, srcDataPath);
        if (!ret.IsDefalutSuccess()) {
            // 软链接创建失败，尝试将数据移回
            utils::MoveDirectory(dstDataPath, srcDataPath);
            return MakeError("Failed to create symlink after migration: " + ret.msg);
        }

        return MakeSuccess();
    }

    // --- 查询接口------

    std::string FileManager::GetServiceConfigPath(const std::string &serviceName) const {
        return GetServiceDir(serviceName) + "/" + ServiceYamlName;
    }

    const FileDirInfo &FileManager::GetCurDirConfig() const {
        return mCurDirConfig;
    }

    // --- 私有路径辅助方法------

    std::string FileManager::GetServiceInitDir() const {
        return mCurDirConfig.serviceDir + "/" + InitDirName;
    }

    std::string FileManager::GetServiceInitFilePath(const std::string &serviceName) const {
        return GetServiceInitDir() + "/" + ServiceFilePrefix + serviceName + ServiceFileSuffix;
    }

    std::string FileManager::GetServiceDir(const std::string &serviceName) const {
        return mCurDirConfig.serviceDir + "/" + serviceName;
    }

    std::string FileManager::GetBackupDir(const std::string &serviceName) const {
        return mCurDirConfig.backupDir + "/" + serviceName;
    }

    std::string FileManager::GetDataDir(const std::string &serviceName) const {
        return mCurDirConfig.dataDir + "/" + serviceName;
    }

    // --- 私有工具方法------

    ResultMsg FileManager::VerifySoftwarePackage(const std::string &softwareDir) {
        // 检查软件包目录存在
        if (!fs::exists(softwareDir)) {
            return MakeError("Software package directory does not exist: " + softwareDir);
        }

        // 检查 service.yaml 存在
        std::string yamlPath = softwareDir + "/" + ServiceYamlName;
        if (!fs::exists(yamlPath)) {
            return MakeError("Software package missing " + std::string(ServiceYamlName));
        }

        // 检查 service.yaml 可读取
        std::ifstream ifs(yamlPath);
        if (!ifs.is_open()) {
            return MakeError("Cannot read " + std::string(ServiceYamlName) + " in package");
        }
        ifs.close();

        // 基本YAML格式验证
        try {
            YAML::LoadFile(yamlPath);
        } catch (const YAML::Exception &e) {
            return MakeError("Invalid YAML format in " + std::string(ServiceYamlName) + ": " + e.what());
        }

        return MakeSuccess();
    }

    ResultMsg FileManager::BackupOldVersion(const std::string &serviceName) {
        std::string serviceDir = GetServiceDir(serviceName);
        std::string backupDir = GetBackupDir(serviceName);

        // 检查服务目录存在
        if (!fs::exists(serviceDir)) {
            return MakeError("Service directory does not exist: " + serviceDir);
        }

        // 如果备份已存在，先清理
        if (fs::exists(backupDir)) {
            auto ret = utils::ForceDeleteDirectory(backupDir);
            if (!ret.IsDefalutSuccess()) {
                return MakeError("Failed to clean existing backup: " + ret.msg);
            }
        }

        // 读取数据目录名，备份时排除数据目录
        std::string dataDirName = ReadServiceDataDirName(serviceName);

        // 复制服务目录到备份目录（排除数据目录）
        return CopyDirectoryExcludeSubDir(serviceDir, backupDir, dataDirName);
    }

    std::string FileManager::ReadServiceDataDirName(const std::string &serviceName) const {
        std::string yamlPath = GetServiceDir(serviceName) + "/" + ServiceYamlName;

        if (!fs::exists(yamlPath)) {
            return "";
        }

        try {
            YAML::Node config = YAML::LoadFile(yamlPath);

            // 读取 execution.dataDir 字段
            if (config["execution"] && config["execution"]["dataDir"]) {
                return config["execution"]["dataDir"].as<std::string>("");
            }
        } catch (const YAML::Exception &) {
            // YAML 解析失败，返回空字符串
        }

        return "";
    }

    ResultMsg FileManager::CopyDirectoryExcludeSubDir(const std::string &src, const std::string &dst,
                                                      const std::string &excludeSubDir) {
        // 如果没有需要排除的子目录，直接使用 utils::CopyDirectory
        if (excludeSubDir.empty()) {
            return utils::CopyDirectory(src, dst);
        }

        // 解析排除路径的第一级目录名
        // 例如 "db/data" 的第一级是 "db"
        std::string firstLevelDir = excludeSubDir;
        size_t slashPos = excludeSubDir.find('/');
        if (slashPos != std::string::npos) {
            firstLevelDir = excludeSubDir.substr(0, slashPos);
        }

        // 创建目标目录
        auto ret = utils::CreateDirectory(dst);
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Failed to create destination directory: " + dst);
        }

        // 遍历源目录，逐项复制（排除指定子目录）
        std::error_code ec;
        for (const auto &entry : fs::directory_iterator(src, ec)) {
            const auto &path = entry.path();
            std::string filename = path.filename().string();

            // 跳过排除的子目录
            if (filename == firstLevelDir && entry.is_directory()) {
                continue;
            }

            fs::path destPath = fs::path(dst) / filename;

            if (entry.is_directory()) {
                ret = utils::CopyDirectory(path.string(), destPath.string());
                if (!ret.IsDefalutSuccess()) {
                    return ret;
                }
            } else {
                fs::copy_file(path, destPath, fs::copy_options::overwrite_existing, ec);
                if (ec) {
                    return MakeError("Failed to copy file: " + path.string() + " -> " + destPath.string() + ", " +
                                     ec.message());
                }
            }
        }

        return MakeSuccess();
    }

    ResultMsg FileManager::ClearDirectoryContentsExclude(const std::string &dir, const std::string &excludeSubDir) {
        // 如果没有需要排除的子目录，直接使用 utils::ClearDirectoryContents
        if (excludeSubDir.empty()) {
            return utils::ClearDirectoryContents(dir);
        }

        // 解析排除路径的第一级目录名
        std::string firstLevelDir = excludeSubDir;
        size_t slashPos = excludeSubDir.find('/');
        if (slashPos != std::string::npos) {
            firstLevelDir = excludeSubDir.substr(0, slashPos);
        }

        // 遍历目录，逐项删除（排除指定子目录）
        std::error_code ec;
        if (!fs::exists(dir, ec)) {
            return MakeSuccess();
        }

        for (const auto &entry : fs::directory_iterator(dir, ec)) {
            const auto &path = entry.path();
            std::string filename = path.filename().string();

            // 跳过排除的子目录
            if (filename == firstLevelDir && entry.is_directory()) {
                continue;
            }

            fs::remove_all(path, ec);
            if (ec) {
                return MakeError("Failed to remove: " + path.string() + ", " + ec.message());
            }
        }

        return MakeSuccess();
    }

}  // namespace qifeng::scm
