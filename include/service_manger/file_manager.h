/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include "common/scmd_def.h"
#include "common/scmd_types.h"

namespace qifeng::scm {

    struct FileDirInfo {
        std::string configDir;
        std::string serviceDir;
        std::string dataDir;
        std::string backupDir;
        std::string logsDir;
        bool operator==(const FileDirInfo &other) const;
    };

    /**
     * @brief 文件管理类
     * 从scmdconfig中获得本系统目录结构，提供：
     * - 系统存储目录结构管理（软件存放位置、.service统一存放）
     * - 软件包解压、验证、cp到指定目录
     * - .service文件统一存储、更新、删除管理
     * - 建立文件与systemd文件的软链接
     * - 数据文件备份、迁移
     * - 软件包的暂备份（升级场景用于回退）
     */
    class FileManager {
    public:
        explicit FileManager(FileDirInfo &&dirConfig);

    public:
        /**
         * @brief 初始化文件目录结构、创建必要的目录
         * @return ResultMsg 操作结果
         */
        ResultMsg InitFileDir();

        /**
         * @brief 替换文件目录、包括数据转移
         * @param newDirConfig 新目录配置信息
         * @return ResultMsg 操作结果
         */
        ResultMsg MigrateFileDir(const FileDirInfo &newDirConfig);

        /**
         * @brief 删除文件目录、包括数据转移
         * @return ResultMsg 操作结果
         */
        ResultMsg DeleteFileDir();

        // --- 软件包管理------

        /**
         * @brief 安装软件包
         * @details 验证软件包完整性、复制到指定目录、验证结构是否完整
         * @param serviceName 服务名称
         * @param softwareDir 软件包目录（已解压）
         * @return ResultMsg 操作结果
         */
        ResultMsg InstallSoftwarePackage(const std::string &serviceName, const std::string &softwareDir);

        /**
         * @brief 获取服务工作目录
         * @details 获取服务工作目录路径
         * @param serviceName 服务名称
         * @return std::string 服务工作目录路径
         */
        std::string GetServiceWDir(const std::string &serviceName) const;

        /**
         * @brief 获取所有已安装服务名称列表
         * @return std::vector<std::string> 所有已安装服务名称列表
         */
        std::vector<std::string> GetAllServicesList();

        /**
         * @brief 升级软件包
         * @details 备份旧版本、验证软件包完整性、替换新版本（保留数据目录）
         * @param serviceName 服务名称
         * @param softwareDir 新软件包目录
         * @return ResultMsg 操作结果
         */
        ResultMsg UpgradeSoftwarePackage(const std::string &serviceName, const std::string &softwareDir);

        /**
         * @brief 删除软件包
         * @details 备份旧版本、删除软件包目录、删除systemd服务文件、删除软链接
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg RemoveSoftwarePackage(const std::string &serviceName);

        /**
         * @brief 回滚软件包
         * @details 从备份目录恢复旧版本软件包（升级失败时使用）
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg RollbackSoftwarePackage(const std::string &serviceName);

        /**
         * @brief 清理备份
         * @details 升级成功后清理备份目录
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg CleanBackup(const std::string &serviceName);

        // --- service文件管理------

        /**
         * @brief 创建service文件
         * @details 创建service文件到.init目录
         * @param fileData service文件内容
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg CreateServiceFile(const std::string &fileData, const std::string &serviceName);

        /**
         * @brief 获取所有service文件名称列表
         * @return std::vector<std::string> 所有service文件名称列表
         */
        std::vector<std::string> GetAllServiceFilesList();

        /**
         * @brief 更新service文件
         * @details 更新service文件内容
         * @param newFileData service文件内容
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg FreshServiceFile(const std::string &newFileData, const std::string &serviceName);

        /**
         * @brief 获取service文件内容
         * @details 获取service文件内容
         * @param serviceName 服务名称
         * @return std::string service文件内容
         */
        std::string GetServiceFileContent(const std::string &serviceName);

        /**
         * @brief 删除service文件
         * @details 删除service文件
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg DeleteServiceFile(const std::string &serviceName);

        // --- 软链接管理------

        /**
         * @brief 创建systemd软链接
         * @details 在 /etc/systemd/system/ 下创建指向 .init 目录中service文件的软链接
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg CreateServiceSymlink(const std::string &serviceName);

        /**
         * @brief 删除systemd软链接
         * @details 删除 /etc/systemd/system/ 下的软链接
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg DeleteServiceSymlink(const std::string &serviceName);

        /**
         * @brief 更新systemd软链接
         * @details 若链接存在、则覆盖创建新链接、若不存在则创建新链接
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg FreshServiceSymlink(const std::string &serviceName);

        // --- 数据目录管理------

        /**
         * @brief 迁移服务数据目录
         * @details 将 .service/xxxx/dataSubDir 迁移到 .data/xxxx/dataSubDir，并在原位置创建软链接
         * @param serviceName 服务名称
         * @param dataSubDir 数据子目录名（相对路径，如 "data" 或 "db/data"）
         * @return ResultMsg 操作结果
         */
        ResultMsg MigrateServiceData(const std::string &serviceName, const std::string &dataSubDir);

        // --- 查询接口------

        /**
         * @brief 获取服务配置文件路径
         * @param serviceName 服务名称
         * @return std::string service.yaml 完整路径
         */
        std::string GetServiceConfigPath(const std::string &serviceName) const;

        /**
         * @brief 获取当前目录配置
         * @return FileDirInfo 当前目录配置信息
         */
        const FileDirInfo &GetCurDirConfig() const;

    private:
        // --路径辅助方法
        /**
         * @brief 获取 .init 目录路径
         * @return std::string .init 目录路径
         */
        std::string GetServiceInitDir() const;

        /**
         * @brief 获取 systemd service 文件路径
         * @param serviceName 服务名称
         * @return std::string scmd_xxxx.service 文件路径
         */
        std::string GetServiceInitFilePath(const std::string &serviceName) const;

        /**
         * @brief 获取服务目录路径
         * @param serviceName 服务名称
         * @return std::string 服务目录路径
         */
        std::string GetServiceDir(const std::string &serviceName) const;

        /**
         * @brief 获取备份目录路径
         * @param serviceName 服务名称
         * @return std::string 备份目录路径
         */
        std::string GetBackupDir(const std::string &serviceName) const;

        /**
         * @brief 获取数据目录路径
         * @param serviceName 服务名称
         * @return std::string 数据目录路径
         */
        std::string GetDataDir(const std::string &serviceName) const;

        // --内部工具
        /**
         * @brief 验证软件包完整性
         * @details 检查软件包目录是否存在、是否包含service.yaml等必要文件
         * @param softwareDir 软件包目录
         * @return ResultMsg 操作结果
         */
        ResultMsg VerifySoftwarePackage(const std::string &softwareDir);

        /**
         * @brief 备份旧版本软件包
         * @details 备份旧版本软件包到备份目录（排除数据目录）
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg BackupOldVersion(const std::string &serviceName);

        /**
         * @brief 从service.yaml读取数据目录名
         * @details 解析service.yaml获取execution.dataDir字段
         * @param serviceName 服务名称
         * @return std::string 数据目录相对路径，未配置则返回空字符串
         */
        std::string ReadServiceDataDirName(const std::string &serviceName) const;

        /**
         * @brief 复制目录但排除指定子目录
         * @details 基于 utils::CopyDirectory 实现，遍历源目录逐项复制时跳过排除的子目录
         * @param src 源目录
         * @param dst 目标目录
         * @param excludeSubDir 需要排除的子目录名
         * @return ResultMsg 操作结果
         */
        ResultMsg CopyDirectoryExcludeSubDir(const std::string &src, const std::string &dst,
                                             const std::string &excludeSubDir);

        /**
         * @brief 清空目录内容但保留目录本身，可选排除指定子目录
         * @details 基于 utils::ClearDirectoryContents 实现，遍历目录逐项删除时跳过排除的子目录
         * @param dir 目录路径
         * @param excludeSubDir 需要保留的子目录名
         * @return ResultMsg 操作结果
         */
        ResultMsg ClearDirectoryContentsExclude(const std::string &dir, const std::string &excludeSubDir);

    private:
        static constexpr const char* InitDirName = ".init";                   // 服务初始化目录名
        static constexpr const char* ServiceYamlName = DefaultServiceName;    // 服务配置文件名
        static constexpr const char* ServiceFilePrefix = "scmd_";             // systemd 服务文件前缀
        static constexpr const char* ServiceFileSuffix = ".service";          // systemd 服务文件后缀
        static constexpr const char* SystemdUnitDir = "/etc/systemd/system";  // systemd 服务目录

        FileDirInfo mCurDirConfig;
    };
}  // namespace qifeng::scm
