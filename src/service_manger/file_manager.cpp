/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#include "common/types.h"
#include "service_manger/file_manager.h"

#include "common/utils.h"
#include "qifeng_framework/common/logger.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

namespace qifeng::scm {

    namespace {
        // 统一拷贝文件或目录到目标路径
        // - 目录：递归拷贝（CopyDirectory 内部自动创建目标目录）
        // - 文件：先确保目标父目录存在，再覆盖拷贝
        // 设计目的：up_detail.yaml 的 replace/add 列表既可填目录也可填文件，
        //           统一入口避免调用方逐处判断类型
        ResultMsg CopyEntry(const std::string &src, const std::string &dst) {
            std::error_code ec;
            if (!fs::exists(src, ec)) {
                return MakeError("Source does not exist: " + src);
            }
            if (fs::is_directory(src)) {
                return utils::CopyDirectory(src, dst);
            }
            // 文件：确保目标父目录存在后拷贝（与目录路径 create_directories 行为对齐）
            fs::path dstPath(dst);
            fs::path parent = dstPath.parent_path();
            if (!parent.empty() && !fs::exists(parent, ec)) {
                fs::create_directories(parent, ec);
                if (ec) {
                    return MakeError("Failed to create parent directory: " + parent.string() + ", " + ec.message());
                }
            }
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                return MakeError("Failed to copy file " + src + " to " + dst + ": " + ec.message());
            }
            return MakeSuccess();
        }
    }  // namespace

    bool FileDirInfo::operator==(const FileDirInfo &other) const {
        return configDir == other.configDir && serviceDir == other.serviceDir && dataDir == other.dataDir &&
               backupDir == other.backupDir && logsDir == other.logsDir && tempDir == other.tempDir;
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

        // 创建 .temp 目录
        ret = utils::CreateDirectory(mCurDirConfig.tempDir);
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Failed to create temp dir: " + ret.msg);
        }

        return MakeSuccess();
    }

    // NOLINTBEGIN(readability-function-size, readability-function-cognitive-complexity)
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
    // NOLINTEND(readability-function-size, readability-function-cognitive-complexity)

    ResultMsg FileManager::DeleteFileDir() {
        // 先删除所有 systemd 软链接
        auto serviceFiles = GetAllServiceFilesList();
        for (const auto &fileName : serviceFiles) {
            utils::DeleteSymbolicLink(utils::JoinPath(SystemdUnitDir, fileName));
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
        std::string softwarePath = utils::JoinPath(softwareDir, serviceName);
        auto ret = VerifySoftwarePackage(softwarePath);
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Software package verification failed: " + ret.msg);
        }

        // 解包路径和目标服务路径不相同则复制软件包到服务目录
        // 使用拷贝而非移动：避免安装/升级失败回退时临时目录中的源（可能来自用户目录）被链式丢失
        if (!utils::IsSamePath(softwarePath, serviceDir).IsDefalutSuccess()) {
            auto copyRet = utils::CopyDirectory(softwarePath, serviceDir);
            if (!copyRet.IsDefalutSuccess()) {
                // 失败时清理已创建的目录
                utils::ForceDeleteDirectory(serviceDir);
                return MakeError("Failed to install software package: " + copyRet.msg);
            }
        }

        // 设置服务目录权限
        ret = utils::SetFilePermission(serviceDir, utils::GetCurrentUserName());
        if (!ret.IsDefalutSuccess()) {
            utils::ForceDeleteDirectory(serviceDir);
            return MakeError("Failed to set service directory permission: " + ret.msg);
        }

        // 验证安装后结构
        std::string installedYaml = utils::JoinPath(serviceDir, ServiceYamlName);
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
            std::string yamlPath = utils::JoinPath(entry.path().string(), ServiceYamlName);
            if (fs::exists(yamlPath)) {
                services.push_back(dirName);
            }
        }

        return services;
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg FileManager::UpgradeSoftwarePackage(const std::string &serviceName, const std::string &softwareDir) {
        std::string serviceDir = GetServiceDir(serviceName);

        // 检查服务已安装
        if (!fs::exists(serviceDir)) {
            return MakeError("Service not installed: " + serviceName);
        }

        // 与 InstallSoftwarePackage 保持一致：实际软件包在 softwareDir/serviceName 子目录下
        std::string actualSoftwareDir = utils::JoinPath(softwareDir, serviceName);

        // 备份旧版本（排除数据目录）
        auto ret = BackupOldVersion(serviceName);
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Failed to backup old version: " + ret.msg);
        }

        // 验证新软件包
        ret = VerifySoftwarePackage(actualSoftwareDir);
        if (!ret.IsDefalutSuccess()) {
            return MakeError("New software package verification failed: " + ret.msg);
        }

        // 读取旧服务的数据目录名
        std::string dataDirName = ReadServiceDataDirName(serviceName);

        // 升级前先将旧数据目录备份到临时位置，防止 CopyDirectory 覆盖
        std::string oldDataDir = utils::JoinPath(serviceDir, dataDirName);
        std::string oldDataBackup = utils::JoinPath(GetBackupDir(serviceName), ".data_backup");
        bool hasOldData = false;
        if (!dataDirName.empty() && fs::exists(oldDataDir)) {
            hasOldData = true;
            if (fs::exists(oldDataBackup)) {
                utils::ForceDeleteDirectory(oldDataBackup);
            }
            auto backupRet = utils::CopyDirectory(oldDataDir, oldDataBackup);
            if (!backupRet.IsDefalutSuccess()) {
                return MakeError("Failed to backup old data directory: " + backupRet.msg);
            }
        }

        // 删除旧服务文件（保留数据目录）
        ret = ClearDirectoryContentsExclude(serviceDir, dataDirName);
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Failed to clear old service files: " + ret.msg);
        }

        // 复制新软件包到服务目录
        auto copyRet = utils::CopyDirectory(actualSoftwareDir, serviceDir);
        if (!copyRet.IsDefalutSuccess()) {
            // 复制失败，尝试回滚
            RollbackSoftwarePackage(serviceName);
            return MakeError("Failed to copy new software package: " + copyRet.msg);
        }

        // 用旧数据目录覆盖新包中的默认数据目录，确保数据复用
        if (hasOldData) {
            std::string newDataDir = utils::JoinPath(serviceDir, dataDirName);
            if (fs::exists(newDataDir)) {
                utils::ForceDeleteDirectory(newDataDir);
            }
            auto restoreRet = utils::CopyDirectory(oldDataBackup, newDataDir);
            if (!restoreRet.IsDefalutSuccess()) {
                SLOG_ERROR << "Failed to restore old data directory: " << restoreRet.msg;
            }
            utils::ForceDeleteDirectory(oldDataBackup);
        }

        // 验证安装后结构
        std::string installedYaml = utils::JoinPath(serviceDir, ServiceYamlName);
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

        // 设置服务文件权限
        ret = utils::SetFilePermission(filePath, utils::GetCurrentUserName());
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Failed to set service file permission: " + ret.msg);
        }

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
        std::string linkPath = utils::JoinPath(SystemdUnitDir, ServiceFilePrefix + serviceName + ServiceFileSuffix);
        std::string targetPath = utils::GetAbsolutePath(initFilePath);

        return utils::CreateSymbolicLink(targetPath, linkPath);
    }

    ResultMsg FileManager::DeleteServiceSymlink(const std::string &serviceName) {
        std::string linkPath = utils::JoinPath(SystemdUnitDir, ServiceFilePrefix + serviceName + ServiceFileSuffix);

        return utils::DeleteSymbolicLink(linkPath);
    }

    ResultMsg FileManager::FreshServiceSymlink(const std::string &serviceName) {
        std::string linkPath = utils::JoinPath(SystemdUnitDir, ServiceFilePrefix + serviceName + ServiceFileSuffix);
        // 检查软链接是否存在
        ResultMsg ret;
        if (fs::exists(linkPath)) {
            ret = DeleteServiceSymlink(serviceName);
        }
        if (!ret.IsDefalutSuccess()) {
            return ret;
        }
        ret = CreateServiceSymlink(serviceName);
        if (!ret.IsDefalutSuccess()) {
            return ret;
        }
        return ret;
    }

    // --- 数据目录管理------

    ResultMsg FileManager::MigrateServiceData(const std::string &serviceName, const std::string &dataSubDir) {
        std::string serviceDir = GetServiceDir(serviceName);
        std::string srcDataPath = utils::JoinPath(serviceDir, dataSubDir);

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
        std::string dstDataPath = utils::JoinPath(dataDir, dataSubDir);

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

    // --- 私有工具方法------

    ResultMsg FileManager::VerifySoftwarePackage(const std::string &softwareDir) {
        // 检查软件包目录存在
        if (!fs::exists(softwareDir)) {
            return MakeError("Software package directory does not exist: " + softwareDir);
        }

        // 检查 service.yaml 存在
        std::string yamlPath = utils::JoinPath(softwareDir, ServiceYamlName);
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
        std::string yamlPath = utils::JoinPath(GetServiceDir(serviceName), ServiceYamlName);

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

    // NOLINTBEGIN(readability-function-size, readability-function-cognitive-complexity)
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
    // NOLINTEND(readability-function-size, readability-function-cognitive-complexity)

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

    // --- 前端 nginx 安装 ---

    bool FileManager::HasFrontend(const std::string &softwareDir) const {
        // 检查 softwareDir 下是否存在 frontend 或 nginx 子目录
        if (softwareDir.empty()) {
            return false;
        }
        if (fs::exists(utils::JoinPath(softwareDir, "frontend"))) {
            return true;
        }
        if (fs::exists(utils::JoinPath(softwareDir, "nginx"))) {
            return true;
        }
        return false;
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg FileManager::InstallFrontend(const std::string &serviceName, const std::string &softwareDir) {
        if (serviceName.empty()) {
            return MakeError("Service name is empty");
        }
        if (!fs::exists(softwareDir)) {
            return MakeError("Software directory does not exist: " + softwareDir);
        }

        // 解析 frontend 源目录：优先 nginx，其次 frontend
        std::string frontendSrc = utils::JoinPath(softwareDir, "nginx");
        if (!fs::exists(frontendSrc)) {
            frontendSrc = utils::JoinPath(softwareDir, "frontend");
            if (!fs::exists(frontendSrc)) {
                return MakeError("No frontend/nginx directory found in: " + softwareDir);
            }
        }

        // 创建服务目录下的 nginx 子目录
        std::string serviceDir = GetServiceDir(serviceName);
        std::string nginxDst = utils::JoinPath(serviceDir, "nginx");
        if (fs::exists(nginxDst)) {
            auto ret = utils::ForceDeleteDirectory(nginxDst);
            if (!ret.IsDefalutSuccess()) {
                return MakeError("Failed to clean existing nginx directory: " + ret.msg);
            }
        }
        auto ret = utils::CreateDirectory(nginxDst);
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Failed to create nginx directory: " + ret.msg);
        }

        // 拷贝 frontend/nginx 内容到服务 nginx 目录
        ret = utils::CopyDirectory(frontendSrc, nginxDst);
        if (!ret.IsDefalutSuccess()) {
            utils::ForceDeleteDirectory(nginxDst);
            return MakeError("Failed to copy frontend files: " + ret.msg);
        }

        // ---- 替换 conf 文件中的占位符 ----
        std::string confDir = utils::JoinPath(nginxDst, "conf.d");
        if (fs::exists(confDir)) {
            for (const auto &entry : fs::directory_iterator(confDir)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".conf") {
                    continue;
                }
                // 读取文件内容
                std::ifstream ifs(entry.path().string());
                if (!ifs) {
                    continue;
                }
                std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                ifs.close();

                // 替换 __TEST_NGINX_ROOT__ 占位符为服务 nginx 目录的实际路径
                const std::string placeholder = "__TEST_NGINX_ROOT__";
                size_t pos = content.find(placeholder);
                if (pos != std::string::npos) {
                    content.replace(pos, placeholder.length(), nginxDst);
                    std::ofstream ofs(entry.path().string(), std::ios::trunc);
                    if (ofs) {
                        ofs << content;
                        ofs.close();
                    }
                }
            }
        }

        // ---- 安装 conf 文件到系统 nginx 配置目录 ----
        auto nginxSystemConfDir = "/etc/nginx/conf.d";
        auto nginxSystemSnippetsDir = "/etc/nginx/snippets";

        // 确保系统目录存在
        std::error_code ec;
        fs::create_directories(nginxSystemConfDir, ec);
        fs::create_directories(nginxSystemSnippetsDir, ec);

        if (fs::exists(confDir)) {
            // 第一遍：识别文件类型并安装到系统目录
            for (const auto &entry : fs::directory_iterator(confDir)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".conf") {
                    continue;
                }
                std::string filename = entry.path().filename().string();
                std::string srcPath = entry.path().string();

                // 读取内容判断是否为 server 块配置
                std::ifstream ifs(srcPath);
                if (!ifs) {
                    continue;
                }
                std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                ifs.close();

                // 包含 "server {" 的文件为 server 块配置，安装到 conf.d；否则安装到 snippets
                std::string systemDstPath;
                if (content.find("server {") != std::string::npos || content.find("server{") != std::string::npos) {
                    // server 块配置 → /etc/nginx/conf.d/<serviceName>_<filename>
                    systemDstPath = std::string(nginxSystemConfDir) + "/" + serviceName + "_" + filename;
                } else {
                    // 非 server 块（如 location 片段）→ /etc/nginx/snippets/<serviceName>_<filename>
                    systemDstPath = std::string(nginxSystemSnippetsDir) + "/" + serviceName + "_" + filename;
                }

                ec.clear();
                fs::copy_file(srcPath, systemDstPath, fs::copy_options::overwrite_existing, ec);
                if (ec) {
                    SLOG_WARN << "Failed to install nginx conf file to " << systemDstPath << ": " << ec.message();
                } else {
                    SLOG_INFO << "Installed nginx conf: " << systemDstPath;
                }
            }

            // 第二遍：更新 server 块 conf 中的 include 路径，使其指向带服务前缀的 snippet 文件
            for (const auto &entry : fs::directory_iterator(confDir)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".conf") {
                    continue;
                }
                std::string filename = entry.path().filename().string();
                std::string srcPath = entry.path().string();

                std::ifstream ifs(srcPath);
                if (!ifs) {
                    continue;
                }
                std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                ifs.close();

                bool modified = false;
                // 替换 include /etc/nginx/snippets/<origName> 为 /etc/nginx/snippets/<serviceName>_<origName>
                const std::string snippetInclude = "include /etc/nginx/snippets/";
                size_t searchPos = 0;
                while ((searchPos = content.find(snippetInclude, searchPos)) != std::string::npos) {
                    size_t nameStart = searchPos + snippetInclude.length();
                    size_t nameEnd = content.find(';', nameStart);
                    if (nameEnd == std::string::npos) {
                        break;
                    }
                    std::string origName = content.substr(nameStart, nameEnd - nameStart);
                    // 去除首尾空白
                    while (!origName.empty() && (origName.front() == ' ' || origName.front() == '\t')) {
                        origName.erase(origName.begin());
                    }
                    while (!origName.empty() && (origName.back() == ' ' || origName.back() == '\t')) {
                        origName.pop_back();
                    }
                    // 仅替换尚未带服务前缀的路径
                    if (origName.find(serviceName + "_") != 0) {
                        std::string newName = serviceName + "_" + origName;
                        content.replace(nameStart, nameEnd - nameStart, newName);
                        modified = true;
                    }
                    searchPos = nameStart + serviceName.length() + 1;
                }

                if (modified) {
                    // 同时更新服务目录和系统 conf.d 中的副本
                    std::ofstream ofs(srcPath, std::ios::trunc);
                    if (ofs) {
                        ofs << content;
                        ofs.close();
                    }
                    std::string systemConfPath = std::string(nginxSystemConfDir) + "/" + serviceName + "_" + filename;
                    if (fs::exists(systemConfPath)) {
                        std::ofstream sysOfs(systemConfPath, std::ios::trunc);
                        if (sysOfs) {
                            sysOfs << content;
                            sysOfs.close();
                        }
                    }
                }
            }
        }

        SLOG_INFO << "Frontend installed to: " << nginxDst;
        return MakeSuccess();
    }

    ResultMsg FileManager::UninstallFrontend(const std::string &serviceName) {
        if (serviceName.empty()) {
            return MakeError("Service name is empty");
        }

        // 清理 conf.d 和 snippets 中由 scm 创建的软链（目标指向 service/nginx/ 目录）
        std::error_code ec;
        std::string nginxDst = GetNginxDir();
        auto targetDirs = {"/etc/nginx/conf.d", "/etc/nginx/snippets"};

        for (const auto &dir : targetDirs) {
            if (!fs::exists(dir)) {
                continue;
            }
            for (const auto &entry : fs::directory_iterator(dir, ec)) {
                if (ec) {
                    continue;
                }
                if (!fs::is_symlink(entry.path(), ec)) {
                    continue;
                }
                // 检查软链目标是否指向 service/nginx 目录
                auto target = fs::read_symlink(entry.path(), ec);
                if (ec) {
                    continue;
                }
                if (target.string().find(nginxDst) != std::string::npos) {
                    fs::remove(entry.path(), ec);
                    if (ec) {
                        SLOG_WARN << "Failed to remove nginx symlink: " << entry.path().string() << ": "
                                  << ec.message();
                    } else {
                        SLOG_INFO << "Removed nginx symlink: " << entry.path().string();
                    }
                }
            }
        }

        return MakeSuccess();
    }

    std::string FileManager::GetFrontendDir(const std::string &serviceName) const {
        return utils::JoinPath(GetServiceDir(serviceName), "nginx");
    }

    // --- 独立 nginx 配置管理 ---

    std::string FileManager::GetNginxDir() const {
        return utils::JoinPath(mCurDirConfig.serviceDir, "nginx");
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg FileManager::InitNginx(const std::string &srcPath) {
        if (!fs::exists(srcPath)) {
            return MakeError("Source path does not exist: " + srcPath);
        }

        // 识别 nginx 根目录：优先直接包含 conf.d/frontend 的目录，
        // 其次 srcPath/nginx，最后兼容 srcPath/frontend
        auto hasNginxStructure = [](const std::string &path) -> bool {
            return fs::exists(utils::JoinPath(path, "conf.d")) || fs::exists(utils::JoinPath(path, "frontend"));
        };

        std::string nginxRootSrc;
        if (hasNginxStructure(srcPath)) {
            nginxRootSrc = srcPath;
        } else {
            std::string nginxSubDir = utils::JoinPath(srcPath, "nginx");
            if (fs::exists(nginxSubDir) && hasNginxStructure(nginxSubDir)) {
                nginxRootSrc = nginxSubDir;
            } else {
                std::string frontendSubDir = utils::JoinPath(srcPath, "frontend");
                if (fs::exists(frontendSubDir)) {
                    nginxRootSrc = frontendSubDir;
                } else {
                    return MakeError("No valid nginx structure (conf.d/frontend) found in: " + srcPath);
                }
            }
        }

        std::string nginxDst = GetNginxDir();

        // 清理旧的系统 conf 软链/文件（使用 scm_ 前缀），避免后续创建软链时冲突
        auto uninstallResult = UninstallFrontend("scm");
        if (!uninstallResult.IsDefalutSuccess()) {
            SLOG_WARN << "Failed to uninstall old nginx conf: " << uninstallResult.msg;
        }

        // 备份已有 nginx 目录（用于回退）
        bool hadExistingNginx = fs::exists(nginxDst);
        std::string nginxBackup;
        if (hadExistingNginx) {
            nginxBackup = utils::JoinPath(mCurDirConfig.backupDir, "nginx_backup");
            if (fs::exists(nginxBackup)) {
                utils::ForceDeleteDirectory(nginxBackup);
            }
            auto backupRet = utils::CopyDirectory(nginxDst, nginxBackup);
            if (!backupRet.IsDefalutSuccess()) {
                SLOG_WARN << "Failed to backup existing nginx directory: " << backupRet.msg;
            }
            // 清除当前 nginx 目录
            auto delRet = utils::ForceDeleteDirectory(nginxDst);
            if (!delRet.IsDefalutSuccess()) {
                return MakeError("Failed to clean existing nginx directory: " + delRet.msg);
            }
        }

        // 创建 nginx 目录
        auto ret = utils::CreateDirectory(nginxDst);
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Failed to create nginx directory: " + ret.msg);
        }

        // 按原结构拷贝 nginx 源内容到 nginx 目录
        std::error_code ec;
        for (const auto &entry : fs::directory_iterator(nginxRootSrc, ec)) {
            if (ec) {
                SLOG_WARN << "Failed to iterate nginx source: " << ec.message();
                continue;
            }
            std::string filename = entry.path().filename().string();
            std::string dstPath = utils::JoinPath(nginxDst, filename);

            if (entry.is_directory()) {
                ret = utils::CopyDirectory(entry.path().string(), dstPath);
            } else {
                ec.clear();
                fs::copy_file(entry.path(), dstPath, fs::copy_options::overwrite_existing, ec);
                if (ec) {
                    ret = MakeError("Failed to copy file: " + entry.path().string() + " -> " + dstPath + ", " +
                                    ec.message());
                } else {
                    ret = MakeSuccess();
                }
            }

            if (!ret.IsDefalutSuccess()) {
                // 拷贝失败，回退
                utils::ForceDeleteDirectory(nginxDst);
                if (hadExistingNginx && fs::exists(nginxBackup)) {
                    utils::CopyDirectory(nginxBackup, nginxDst);
                }
                return MakeError("Failed to copy nginx files: " + ret.msg);
            }
        }

        // ---- 创建系统 nginx 配置软链 ----
        // 命名规范：server_*.conf → conf.d/；snippet_*.conf → snippets/（均用原名）
        const std::string kServerPrefix = "server_";
        const std::string kSnippetPrefix = "snippet_";
        const std::string nginxSystemConfDir = "/etc/nginx/conf.d";
        const std::string nginxSystemSnippetsDir = "/etc/nginx/snippets";

        fs::create_directories(nginxSystemConfDir, ec);
        fs::create_directories(nginxSystemSnippetsDir, ec);

        std::string confDir = utils::JoinPath(nginxDst, "conf.d");
        if (fs::exists(confDir)) {
            // 为 conf.d 中的每个 .conf 创建系统软链（跳过 waiting.conf）
            bool symlinkFailed = false;
            for (const auto &entry : fs::directory_iterator(confDir)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".conf") {
                    continue;
                }
                std::string filename = entry.path().filename().string();

                // 跳过 waiting.conf（仅由 reset_nginx -wait 指令使用，正常配置不安装）
                if (filename == "waiting.conf") {
                    continue;
                }

                std::string srcConfPath = entry.path().string();

                // 通过文件名前缀区分：server_ → conf.d；snippet_ → snippets（均用原名）
                std::string linkPath;
                if (filename.find(kServerPrefix) == 0) {
                    linkPath = nginxSystemConfDir + "/" + filename;
                } else if (filename.find(kSnippetPrefix) == 0) {
                    linkPath = nginxSystemSnippetsDir + "/" + filename;
                } else {
                    SLOG_WARN << "Skipping unrecognized conf (prefix must be server_ or snippet_): " << filename;
                    continue;
                }

                std::string targetPath = utils::GetAbsolutePath(srcConfPath);

                // 删除已存在的同名路径或软链
                ec.clear();
                if (fs::exists(linkPath, ec) || fs::is_symlink(linkPath, ec)) {
                    fs::remove(linkPath, ec);
                    if (ec) {
                        SLOG_WARN << "Failed to remove existing nginx conf link: " << linkPath << ": " << ec.message();
                        symlinkFailed = true;
                        continue;
                    }
                }

                ec.clear();
                fs::create_symlink(targetPath, linkPath, ec);
                if (ec) {
                    SLOG_WARN << "Failed to create nginx conf symlink: " << linkPath << " -> " << targetPath << ": "
                              << ec.message();
                    symlinkFailed = true;
                } else {
                    SLOG_INFO << "Created nginx conf symlink: " << linkPath << " -> " << targetPath;
                }
            }

            if (symlinkFailed) {
                // 软链创建失败，清理已创建的软链并回退本地目录
                SLOG_ERROR << "Failed to create some nginx conf symlinks, rolling back";
                UninstallFrontend("scm");
                utils::ForceDeleteDirectory(nginxDst);
                if (hadExistingNginx && fs::exists(nginxBackup)) {
                    utils::CopyDirectory(nginxBackup, nginxDst);
                }
                return MakeError("Failed to create nginx conf symlinks");
            }
        }

        // ---- 创建 frontend 软链到 /etc/nginx/conf.d/frontend ----
        // conf 中使用相对路径 "./frontend"，nginx 以 /etc/nginx/conf.d 为工作目录
        // 因此需要将 service/nginx/frontend 软链到 /etc/nginx/conf.d/frontend
        std::string frontendSrc = utils::JoinPath(nginxDst, "frontend");
        if (fs::exists(frontendSrc)) {
            std::string frontendLink = utils::JoinPath(nginxSystemConfDir, "frontend");
            ec.clear();
            if (fs::exists(frontendLink, ec) || fs::is_symlink(frontendLink, ec)) {
                fs::remove(frontendLink, ec);
                if (ec) {
                    SLOG_WARN << "Failed to remove existing frontend link: " << ec.message();
                }
            }
            ec.clear();
            std::string frontendTarget = utils::GetAbsolutePath(frontendSrc);
            fs::create_symlink(frontendTarget, frontendLink, ec);
            if (ec) {
                SLOG_WARN << "Failed to create frontend symlink: " << frontendLink << " -> " << frontendTarget << ": "
                          << ec.message();
            } else {
                SLOG_INFO << "Created frontend symlink: " << frontendLink << " -> " << frontendTarget;
            }
        }

        // 禁用系统默认站点，避免与 scm 默认站点产生 duplicate default server 冲突
        auto disableRet = DisableSystemDefaultSite();
        if (!disableRet.IsDefalutSuccess()) {
            SLOG_ERROR << "Failed to disable system default site, rolling back: " << disableRet.msg;
            UninstallFrontend("scm");
            RestoreSystemDefaultSite();
            utils::ForceDeleteDirectory(nginxDst);
            if (hadExistingNginx && fs::exists(nginxBackup)) {
                utils::CopyDirectory(nginxBackup, nginxDst);
            }
            return MakeError("Failed to disable system default site: " + disableRet.msg);
        }

        // 清理备份（安装成功）
        if (hadExistingNginx && fs::exists(nginxBackup)) {
            utils::ForceDeleteDirectory(nginxBackup);
        }

        SLOG_INFO << "Nginx initialized at " << nginxDst;
        return MakeSuccess();
    }

    ResultMsg FileManager::ResetNginx() {
        // 1. 卸载系统 nginx conf 软链（使用 scm_ 前缀）
        auto uninstallResult = UninstallFrontend("scm");
        if (!uninstallResult.IsDefalutSuccess()) {
            SLOG_WARN << "Failed to uninstall nginx conf: " << uninstallResult.msg;
        }

        // 2. 移除 waiting.conf 软链（若存在）
        const std::string waitingLink = "/etc/nginx/conf.d/scm_waiting.conf";
        std::error_code ec;
        if (fs::exists(waitingLink, ec) || fs::is_symlink(waitingLink, ec)) {
            fs::remove(waitingLink, ec);
            if (ec) {
                SLOG_WARN << "Failed to remove waiting.conf: " << ec.message();
            }
        }

        // 3. 移除 frontend 软链（若存在）
        const std::string frontendLink = "/etc/nginx/conf.d/frontend";
        if (fs::exists(frontendLink, ec) || fs::is_symlink(frontendLink, ec)) {
            fs::remove(frontendLink, ec);
            if (ec) {
                SLOG_WARN << "Failed to remove frontend link: " << ec.message();
            }
        }

        // 4. 恢复系统默认站点
        auto restoreRet = RestoreSystemDefaultSite();
        if (!restoreRet.IsDefalutSuccess()) {
            SLOG_WARN << "Failed to restore system default site: " << restoreRet.msg;
        }

        // 5. 删除 nginx 目录
        std::string nginxDst = GetNginxDir();
        if (fs::exists(nginxDst)) {
            auto delRet = utils::ForceDeleteDirectory(nginxDst);
            if (!delRet.IsDefalutSuccess()) {
                return MakeError("Failed to delete nginx directory: " + delRet.msg);
            }
            SLOG_INFO << "Deleted nginx directory: " << nginxDst;
        } else {
            SLOG_INFO << "No nginx directory found";
        }

        return MakeSuccess();
    }

    ResultMsg FileManager::SetNginxWaiting() {
        // 1. 检查 nginx 目录中是否存在 waiting.conf
        std::string nginxDst = GetNginxDir();
        std::string waitingSrc = utils::JoinPath(nginxDst, "conf.d", "waiting.conf");

        if (!fs::exists(waitingSrc)) {
            return MakeError("waiting.conf not found in: " + waitingSrc +
                             ", please ensure the nginx package contains conf.d/waiting.conf");
        }

        // 2. 移除现有的 scm_*.conf 软链（conf.d 和 snippets），避免 default_server 冲突
        auto uninstallResult = UninstallFrontend("scm");
        if (!uninstallResult.IsDefalutSuccess()) {
            SLOG_WARN << "Failed to uninstall nginx conf: " << uninstallResult.msg;
        }

        std::error_code ec;

        // 3. 保留 frontend 软链
        // 等待状态需要显示 /upSysing 加载页，该页面由前端 SPA 渲染，
        // 因此仍需访问 index.html 与 /assets/ 静态资源，frontend 软链不能移除。
        // 若软链缺失（例如未执行过 init_nginx），此处尝试重建以保证加载页可用。
        const std::string frontendLink = "/etc/nginx/conf.d/frontend";
        std::string frontendSrc = utils::JoinPath(nginxDst, "frontend");
        if (!fs::exists(frontendLink, ec) && !fs::is_symlink(frontendLink, ec)) {
            if (fs::exists(frontendSrc, ec)) {
                fs::create_directories("/etc/nginx/conf.d", ec);
                std::string frontendTarget = utils::GetAbsolutePath(frontendSrc);
                fs::create_symlink(frontendTarget, frontendLink, ec);
                if (ec) {
                    SLOG_WARN << "Failed to create frontend symlink for waiting page: " << ec.message();
                } else {
                    SLOG_INFO << "Created frontend symlink for waiting page: " << frontendLink << " -> " << frontendTarget;
                }
            } else {
                SLOG_WARN << "frontend directory not found, /upSysing loading page may be unavailable: " << frontendSrc;
            }
        }

        // 4. 移除已有的 scm_waiting.conf（确保幂等）
        const std::string waitingLink = "/etc/nginx/conf.d/scm_waiting.conf";
        if (fs::exists(waitingLink, ec) || fs::is_symlink(waitingLink, ec)) {
            fs::remove(waitingLink, ec);
            if (ec) {
                return MakeError("Failed to remove existing waiting.conf: " + ec.message());
            }
        }

        // 4. 创建 waiting.conf 软链
        fs::create_directories("/etc/nginx/conf.d", ec);
        std::string targetPath = utils::GetAbsolutePath(waitingSrc);
        fs::create_symlink(targetPath, waitingLink, ec);
        if (ec) {
            return MakeError("Failed to create symlink: " + waitingLink + " -> " + targetPath + ": " + ec.message());
        }
        SLOG_INFO << "Installed waiting.conf: " << waitingLink << " -> " << targetPath;

        // 5. 禁用系统默认站点（避免 default_server 冲突）
        auto disableRet = DisableSystemDefaultSite();
        if (!disableRet.IsDefalutSuccess()) {
            SLOG_WARN << "Failed to disable system default site: " << disableRet.msg;
        }

        return MakeSuccess();
    }

    ResultMsg FileManager::SetNginxNormal() {
        // 1. 移除 waiting.conf
        const std::string waitingLink = "/etc/nginx/conf.d/scm_waiting.conf";
        std::error_code ec;
        if (fs::exists(waitingLink, ec) || fs::is_symlink(waitingLink, ec)) {
            fs::remove(waitingLink, ec);
            if (ec) {
                return MakeError("Failed to remove waiting.conf: " + ec.message());
            }
            SLOG_INFO << "Removed waiting.conf";
        } else {
            SLOG_INFO << "No waiting.conf found, nothing to remove";
        }

        // 2. 重新创建 scm_*.conf 软链
        std::string nginxDst = GetNginxDir();
        std::string confDir = utils::JoinPath(nginxDst, "conf.d");
        if (!fs::exists(confDir)) {
            return MakeError("Nginx conf.d directory not found: " + confDir + ", please run init_nginx first");
        }

        const std::string kConfPrefix = "scm_";
        const std::string nginxSystemConfDir = "/etc/nginx/conf.d";
        const std::string nginxSystemSnippetsDir = "/etc/nginx/snippets";

        fs::create_directories(nginxSystemConfDir, ec);
        fs::create_directories(nginxSystemSnippetsDir, ec);

        for (const auto &entry : fs::directory_iterator(confDir, ec)) {
            if (ec) {
                continue;
            }
            if (!entry.is_regular_file() || entry.path().extension() != ".conf") {
                continue;
            }

            // 跳过 waiting.conf（不属于正常配置）
            if (entry.path().filename().string() == "waiting.conf") {
                continue;
            }

            std::string filename = entry.path().filename().string();
            std::string srcConfPath = entry.path().string();

            // 通过文件名前缀区分：server_ → conf.d；snippet_ → snippets（均用原名）
            const std::string kServerPrefix = "server_";
            const std::string kSnippetPrefix = "snippet_";
            std::string linkPath;
            if (filename.find(kServerPrefix) == 0) {
                linkPath = nginxSystemConfDir + "/" + filename;
            } else if (filename.find(kSnippetPrefix) == 0) {
                linkPath = nginxSystemSnippetsDir + "/" + filename;
            } else {
                continue;
            }

            std::string targetPath = utils::GetAbsolutePath(srcConfPath);

            // 移除已存在的同名文件/软链
            ec.clear();
            if (fs::exists(linkPath, ec) || fs::is_symlink(linkPath, ec)) {
                fs::remove(linkPath, ec);
                if (ec) {
                    SLOG_WARN << "Failed to remove existing nginx conf link: " << linkPath;
                    continue;
                }
            }

            ec.clear();
            fs::create_symlink(targetPath, linkPath, ec);
            if (ec) {
                SLOG_WARN << "Failed to create symlink: " << linkPath << " -> " << targetPath << ": " << ec.message();
            } else {
                SLOG_INFO << "Created nginx conf symlink: " << linkPath << " -> " << targetPath;
            }
        }

        // 3. 创建 frontend 软链（与 InitNginx 一致）
        std::string frontendSrc = utils::JoinPath(nginxDst, "frontend");
        if (fs::exists(frontendSrc)) {
            std::string frontendLink = utils::JoinPath(nginxSystemConfDir, "frontend");
            ec.clear();
            if (fs::exists(frontendLink, ec) || fs::is_symlink(frontendLink, ec)) {
                fs::remove(frontendLink, ec);
            }
            ec.clear();
            std::string frontendTarget = utils::GetAbsolutePath(frontendSrc);
            fs::create_symlink(frontendTarget, frontendLink, ec);
            if (ec) {
                SLOG_WARN << "Failed to create frontend symlink: " << frontendLink << " -> " << frontendTarget << ": "
                          << ec.message();
            } else {
                SLOG_INFO << "Created frontend symlink: " << frontendLink << " -> " << frontendTarget;
            }
        }

        // 4. 禁用系统默认站点（避免 default_server 冲突）
        auto disableRet = DisableSystemDefaultSite();
        if (!disableRet.IsDefalutSuccess()) {
            SLOG_WARN << "Failed to disable system default site: " << disableRet.msg;
        }

        return MakeSuccess();
    }

    ResultMsg FileManager::DisableSystemDefaultSite() {
        std::error_code ec;
        if (!fs::exists(NginxSystemDefaultSite, ec)) {
            return MakeSuccess();
        }

        auto dirRet = utils::CreateDirectory(mCurDirConfig.backupDir);
        if (!dirRet.IsDefalutSuccess()) {
            return MakeError("Failed to create backup directory: " + dirRet.msg);
        }

        std::string backupPath = utils::JoinPath(mCurDirConfig.backupDir, NginxSitesDefaultBackup);

        // 若已存在历史备份，说明上次 init 已保留过原始站点；本次仅需移除当前系统站点
        ec.clear();
        if (fs::exists(backupPath, ec)) {
            fs::remove(NginxSystemDefaultSite, ec);
            if (ec) {
                return MakeError("Failed to remove system default site: " + ec.message());
            }
            SLOG_INFO << "System default site already backed up, removed current: " << NginxSystemDefaultSite;
            return MakeSuccess();
        }

        // 首次禁用：将系统默认站点移动到备份目录
        ec.clear();
        fs::rename(NginxSystemDefaultSite, backupPath, ec);
        if (ec) {
            // rename 失败时尝试复制后删除
            fs::copy_file(NginxSystemDefaultSite, backupPath, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                return MakeError("Failed to backup system default site: " + ec.message());
            }
            fs::remove(NginxSystemDefaultSite, ec);
            if (ec) {
                return MakeError("Failed to remove system default site after backup: " + ec.message());
            }
        }

        SLOG_INFO << "Disabled system default site: " << NginxSystemDefaultSite << " -> " << backupPath;
        return MakeSuccess();
    }

    ResultMsg FileManager::RestoreSystemDefaultSite() {
        std::string backupPath = utils::JoinPath(mCurDirConfig.backupDir, NginxSitesDefaultBackup);
        std::error_code ec;
        if (!fs::exists(backupPath, ec)) {
            return MakeSuccess();
        }

        fs::create_directories(fs::path(NginxSystemDefaultSite).parent_path(), ec);

        // 若目标已存在，先移除
        ec.clear();
        if (fs::exists(NginxSystemDefaultSite, ec) || fs::is_symlink(NginxSystemDefaultSite, ec)) {
            fs::remove(NginxSystemDefaultSite, ec);
            if (ec) {
                return MakeError("Failed to remove existing system default site: " + ec.message());
            }
        }

        // 恢复备份
        ec.clear();
        fs::rename(backupPath, NginxSystemDefaultSite, ec);
        if (ec) {
            fs::copy_file(backupPath, NginxSystemDefaultSite, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                return MakeError("Failed to restore system default site: " + ec.message());
            }
            fs::remove(backupPath, ec);
        }

        SLOG_INFO << "Restored system default site: " << NginxSystemDefaultSite;
        return MakeSuccess();
    }

    // --- 细粒度升级 ---

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg FileManager::ParseUpgradeDetail(const std::string &detailPath, UpgradeDetail &outDetail) {
        outDetail.replaceDirs.clear();
        outDetail.addDirs.clear();
        outDetail.removeDirs.clear();

        if (detailPath.empty()) {
            return MakeError("up_detail.yaml path is empty");
        }
        if (!fs::exists(detailPath)) {
            return MakeError("up_detail.yaml not found: " + detailPath);
        }

        try {
            YAML::Node root = YAML::LoadFile(detailPath);
            if (root["replace"]) {
                for (const auto &item : root["replace"]) {
                    std::string name = item.as<std::string>("");
                    if (!name.empty()) {
                        outDetail.replaceDirs.push_back(name);
                    }
                }
            }
            if (root["add"]) {
                for (const auto &item : root["add"]) {
                    std::string name = item.as<std::string>("");
                    if (!name.empty()) {
                        outDetail.addDirs.push_back(name);
                    }
                }
            }
            if (root["remove"]) {
                for (const auto &item : root["remove"]) {
                    std::string name = item.as<std::string>("");
                    if (!name.empty()) {
                        outDetail.removeDirs.push_back(name);
                    }
                }
            }
        } catch (const YAML::Exception &e) {
            return MakeError("Failed to parse up_detail.yaml: " + std::string(e.what()));
        }

        SLOG_INFO << "Parsed upgrade detail: replace=" << outDetail.replaceDirs.size()
                  << " add=" << outDetail.addDirs.size() << " remove=" << outDetail.removeDirs.size();
        return MakeSuccess();
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg FileManager::BackupForFineGrainedUpgrade(const std::string &serviceName, const UpgradeDetail &detail) {
        std::string serviceDir = GetServiceDir(serviceName);
        std::string backupRoot = GetBackupDir(serviceName);
        std::string filesBackup = utils::JoinPath(backupRoot, "files_backup");

        // 确保备份根目录存在
        auto ret = utils::CreateDirectory(backupRoot);
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Failed to create backup root: " + ret.msg);
        }
        // 若已有 files_backup 先清理
        if (fs::exists(filesBackup)) {
            utils::ForceDeleteDirectory(filesBackup);
        }
        ret = utils::CreateDirectory(filesBackup);
        if (!ret.IsDefalutSuccess()) {
            return MakeError("Failed to create files_backup: " + ret.msg);
        }

        // 强制备份 service.yaml：ApplyFineGrainedUpgrade 会无条件覆盖它，回滚时必须能恢复
        // 不依赖 up_detail.yaml 的 replace 列表是否包含 service.yaml
        {
            std::string yamlSrc = utils::JoinPath(serviceDir, DefaultServiceName);
            if (fs::exists(yamlSrc)) {
                std::string yamlDst = utils::JoinPath(filesBackup, DefaultServiceName);
                auto yamlRet = CopyEntry(yamlSrc, yamlDst);
                if (!yamlRet.IsDefalutSuccess()) {
                    return MakeError("Failed to backup service.yaml: " + yamlRet.msg);
                }
            }
        }

        // 备份 replace 和 remove 列表中服务目录下存在的文件/目录
        // 使用 CopyEntry 统一处理：目录走 CopyDirectory，文件走 copy_file
        auto backupEntry = [&serviceDir, &filesBackup](const std::string &name) -> ResultMsg {
            std::string src = utils::JoinPath(serviceDir, name);
            if (!fs::exists(src)) {
                return MakeSuccess();
            }
            std::string dst = utils::JoinPath(filesBackup, name);
            return CopyEntry(src, dst);
        };

        for (const auto &dir : detail.replaceDirs) {
            auto r = backupEntry(dir);
            if (!r.IsDefalutSuccess()) {
                return MakeError("Failed to backup replace entry " + dir + ": " + r.msg);
            }
        }
        for (const auto &dir : detail.removeDirs) {
            auto r = backupEntry(dir);
            if (!r.IsDefalutSuccess()) {
                return MakeError("Failed to backup remove entry " + dir + ": " + r.msg);
            }
        }

        // 若 frontend 在 replace 列表中且服务依赖 nginx，备份 nginx.conf
        bool frontendInReplace = false;
        for (const auto &dir : detail.replaceDirs) {
            if (dir == "frontend" || dir == "nginx") {
                frontendInReplace = true;
                break;
            }
        }
        if (frontendInReplace && fs::exists(GetFrontendDir(serviceName))) {
            std::string frontendDir = GetFrontendDir(serviceName);
            std::string nginxBackup = utils::JoinPath(backupRoot, "nginx_backup");
            // 备份整个 nginx 目录（含 conf.d 和 frontend）
            auto backupRet = utils::CopyDirectory(frontendDir, nginxBackup);
            if (!backupRet.IsDefalutSuccess()) {
                SLOG_WARN << "Failed to backup nginx directory: " << backupRet.msg;
            }
        }

        SLOG_INFO << "Fine-grained backup completed for service: " << serviceName;
        return MakeSuccess();
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg FileManager::ApplyFineGrainedUpgrade(const std::string &serviceName, const std::string &sourceDir,
                                                   const UpgradeDetail &detail) {
        std::string serviceDir = GetServiceDir(serviceName);

        // 0. 强制覆盖 service.yaml：服务元信息文件必须随升级整体替换，不受 up_detail.yaml 管辖
        //    后续 ConfigLoader::UpgradeService 会重新解析新 service.yaml，解析失败则触发回退
        std::string srcYaml = utils::JoinPath(sourceDir, DefaultServiceName);
        std::string dstYaml = utils::JoinPath(serviceDir, DefaultServiceName);
        if (fs::exists(srcYaml)) {
            auto yamlRet = CopyEntry(srcYaml, dstYaml);
            if (!yamlRet.IsDefalutSuccess()) {
                return MakeError("Failed to replace service.yaml: " + yamlRet.msg);
            }
            SLOG_INFO << "Replaced service.yaml for service: " << serviceName;
        } else {
            SLOG_WARN << "New service.yaml not found in source dir, skip replace: " << srcYaml;
        }

        // 1. replace: 删除服务目录下同名文件/目录后从源拷贝
        // ForceDeleteDirectory 内部使用 remove_all，对文件和目录均适用
        for (const auto &dir : detail.replaceDirs) {
            std::string srcDir = utils::JoinPath(sourceDir, dir);
            std::string dstDir = utils::JoinPath(serviceDir, dir);
            if (!fs::exists(srcDir)) {
                SLOG_WARN << "Replace source not exists, skip: " << srcDir;
                continue;
            }
            if (fs::exists(dstDir)) {
                auto ret = utils::ForceDeleteDirectory(dstDir);
                if (!ret.IsDefalutSuccess()) {
                    return MakeError("Failed to delete existing entry " + dir + ": " + ret.msg);
                }
            }
            auto ret = CopyEntry(srcDir, dstDir);
            if (!ret.IsDefalutSuccess()) {
                return MakeError("Failed to copy replace entry " + dir + ": " + ret.msg);
            }
            SLOG_INFO << "Replaced entry: " << dir;
        }

        // 2. remove: 仅删除服务目录下对应文件/目录
        for (const auto &dir : detail.removeDirs) {
            std::string dstDir = utils::JoinPath(serviceDir, dir);
            if (fs::exists(dstDir)) {
                auto ret = utils::ForceDeleteDirectory(dstDir);
                if (!ret.IsDefalutSuccess()) {
                    return MakeError("Failed to remove entry " + dir + ": " + ret.msg);
                }
                SLOG_INFO << "Removed entry: " << dir;
            }
        }

        // 3. add: 从源拷贝对应文件/目录到服务目录
        for (const auto &dir : detail.addDirs) {
            std::string srcDir = utils::JoinPath(sourceDir, dir);
            std::string dstDir = utils::JoinPath(serviceDir, dir);
            if (!fs::exists(srcDir)) {
                SLOG_WARN << "Add source not exists, skip: " << srcDir;
                continue;
            }
            if (fs::exists(dstDir)) {
                return MakeError("Add target already exists: " + dir);
            }
            auto ret = CopyEntry(srcDir, dstDir);
            if (!ret.IsDefalutSuccess()) {
                return MakeError("Failed to copy add entry " + dir + ": " + ret.msg);
            }
            SLOG_INFO << "Added entry: " << dir;
        }

        SLOG_INFO << "Fine-grained upgrade applied for service: " << serviceName;
        return MakeSuccess();
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    ResultMsg FileManager::RollbackFineGrainedUpgrade(const std::string &serviceName) {
        std::string serviceDir = GetServiceDir(serviceName);
        std::string backupRoot = GetBackupDir(serviceName);
        std::string filesBackup = utils::JoinPath(backupRoot, "files_backup");

        if (!fs::exists(filesBackup)) {
            return MakeError("files_backup not found for service: " + serviceName);
        }

        // 遍历 files_backup 下每个条目（文件或目录），覆盖恢复到服务目录
        // 使用 CopyEntry 统一处理，保证 service.yaml 等文件也能正确回滚
        std::error_code ec;
        for (const auto &entry : fs::directory_iterator(filesBackup, ec)) {
            std::string name = entry.path().filename().string();
            std::string dst = utils::JoinPath(serviceDir, name);
            if (fs::exists(dst)) {
                auto ret = utils::ForceDeleteDirectory(dst);
                if (!ret.IsDefalutSuccess()) {
                    SLOG_WARN << "Failed to delete entry during rollback: " << name;
                }
            }
            auto ret = CopyEntry(entry.path().string(), dst);
            if (!ret.IsDefalutSuccess()) {
                return MakeError("Failed to restore entry " + name + ": " + ret.msg);
            }
            SLOG_INFO << "Restored entry: " << name;
        }

        // 恢复 nginx 目录（若存在 nginx_backup）
        std::string nginxBackup = utils::JoinPath(backupRoot, "nginx_backup");
        if (fs::exists(nginxBackup)) {
            std::string frontendDir = GetFrontendDir(serviceName);
            // 清除当前 nginx 目录并从备份恢复
            if (fs::exists(frontendDir)) {
                utils::ForceDeleteDirectory(frontendDir);
            }
            auto restoreRet = utils::CopyDirectory(nginxBackup, frontendDir);
            if (!restoreRet.IsDefalutSuccess()) {
                SLOG_WARN << "Failed to restore nginx directory: " << restoreRet.msg;
            }
        }

        SLOG_INFO << "Fine-grained rollback completed for service: " << serviceName;
        return MakeSuccess();
    }

    // --- 解压目录直接安装 ---

    ResultMsg FileManager::InstallSoftwarePackageFromDir(const std::string &serviceName,
                                                         const std::string &softwareDir) {
        // InstallSoftwarePackage 已支持 softwareDir/<serviceName> 形式，直接复用
        return InstallSoftwarePackage(serviceName, softwareDir);
    }

    void FileManager::CleanupService(const std::string &serviceName) {
        // 删除服务目录
        RemoveSoftwarePackage(serviceName);

        // 删除systemd服务文件
        DeleteServiceFile(serviceName);

        // 删除软链接
        DeleteServiceSymlink(serviceName);
    }

}  // namespace qifeng::scm
