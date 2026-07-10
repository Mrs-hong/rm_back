/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include "common/scmd_def.h"
#include "common/types.h"
#include <vector>
namespace qifeng::scm {

    struct FileDirInfo {
        std::string configDir;
        std::string serviceDir;
        std::string dataDir;
        std::string backupDir;
        std::string logsDir;
        std::string tempDir;
        bool operator==(const FileDirInfo &other) const;
    };

    /**
     * @brief 升级动作枚举（细粒度升级）
     * @details 对应 up_detail.yaml 中的 replace/add/remove 三类操作
     */
    enum class UpgradeAction { REPLACE, ADD, REMOVE };

    /**
     * @brief 细粒度升级方案
     * @details 来自 up_detail.yaml，仅记录目录名（相对软件包根目录的第一级目录）
     */
    struct UpgradeDetail {
        std::vector<std::string> replaceDirs;  // 需替换的目录名列表
        std::vector<std::string> addDirs;      // 需新增的目录名列表
        std::vector<std::string> removeDirs;   // 需删除的目录名列表
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
         * @details 删除服务目录、删除systemd服务文件、删除软链接
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

        // --- 前端 nginx 安装 ---

        /**
         * @brief 判断软件包是否包含前端资源
         * @details 检查 softwareDir 下是否存在 frontend 或 nginx 子目录
         * @param softwareDir 服务软件目录（含 bin、nginx 等子目录）
         * @return bool true 表示包含前端资源
         */
        bool HasFrontend(const std::string &softwareDir) const;

        /**
         * @brief 安装前端 nginx 资源并集成到系统 nginx
         * @details 将 softwareDir/nginx 内容拷贝到 serviceDir/nginx，
         *          替换 __TEST_NGINX_ROOT__ 占位符，
         *          将 server 块 conf 安装到 /etc/nginx/conf.d/<serviceName>_<filename>，
         *          将非 server 块 conf 安装到 /etc/nginx/snippets/<serviceName>_<filename>，
         *          更新 include 路径以匹配新文件名
         * @param serviceName 服务名称
         * @param softwareDir 软件包根目录（含 nginx 子目录）
         * @return ResultMsg 操作结果
         */
        ResultMsg InstallFrontend(const std::string &serviceName, const std::string &softwareDir);

        /**
         * @brief 卸载前端 nginx 配置
         * @details 删除 /etc/nginx/conf.d/<serviceName>_* 和 /etc/nginx/snippets/<serviceName>_*
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg UninstallFrontend(const std::string &serviceName);

        /**
         * @brief 获取服务前端 nginx 配置目录路径
         * @details 返回 <serviceDir>/nginx
         * @param serviceName 服务名称
         * @return std::string 配置目录路径
         */
        std::string GetFrontendDir(const std::string &serviceName) const;

        // --- 独立 nginx 配置管理 ---

        /**
         * @brief 获取 nginx 配置目录路径
         * @details 返回 <serviceDir>/nginx（固定路径，不绑定具体服务）
         * @return std::string nginx 配置目录路径
         */
        std::string GetNginxDir() const;

        /**
         * @brief 独立初始化 nginx 配置
         * @details 将 srcPath 中的 nginx/frontend 内容拷贝到 <serviceDir>/nginx/，
         *          替换 __TEST_NGINX_ROOT__ 占位符，安装 conf 到系统 nginx 目录（使用 scm_ 前缀）。
         *          若已有 nginx 目录，先备份到 backupDir，安装失败时自动回退。
         * @param srcPath nginx 配置源路径（目录，含 nginx/ 或 frontend/ 子目录）
         * @return ResultMsg 操作结果
         */
        ResultMsg InitNginx(const std::string &srcPath);

        /**
         * @brief 重置 nginx 配置
         * @details 删除 /etc/nginx/conf.d/scm_* 和 /etc/nginx/snippets/scm_*，
         *          删除 <serviceDir>/nginx/ 目录
         * @return ResultMsg 操作结果
         */
        ResultMsg ResetNginx();

        /**
         * @brief 设置 nginx 为等待状态
         * @details 安装 waiting.conf 到 /etc/nginx/conf.d/scm_waiting.conf，
         *          同时移除其他 scm_*.conf 软链避免冲突，禁用系统默认站点。
         *          需要服务 nginx 目录中存在 waiting.conf 模板。
         * @return ResultMsg 操作结果
         */
        ResultMsg SetNginxWaiting();

        /**
         * @brief 恢复 nginx 为正常状态
         * @details 移除 /etc/nginx/conf.d/scm_waiting.conf，
         *          重新创建 scm_*.conf 软链使原配置生效，
         *          禁用系统默认站点（避免 default_server 冲突）
         * @return ResultMsg 操作结果
         */
        ResultMsg SetNginxNormal();

        // --- 细粒度升级 ---

        /**
         * @brief 解析 up_detail.yaml
         * @param detailPath 配置文件路径
         * @param outDetail 输出参数，解析结果
         * @return ResultMsg 操作结果（文件不存在或解析失败时返回错误）
         */
        ResultMsg ParseUpgradeDetail(const std::string &detailPath, UpgradeDetail &outDetail);

        /**
         * @brief 细粒度备份（只备份将被影响的内容）
         * @details 将服务目录下 replaceDirs/removeDirs 对应的目录拷贝到
         *          backupDir/<serviceName>/files_backup；
         *          若 frontend 在 replaceDirs 中且服务依赖 nginx，备份 nginx.conf 到 nginx_backup
         * @param serviceName 服务名称
         * @param detail 升级方案
         * @return ResultMsg 操作结果
         */
        ResultMsg BackupForFineGrainedUpgrade(const std::string &serviceName, const UpgradeDetail &detail);

        /**
         * @brief 执行细粒度文件升级
         * @details 按 replace → remove → add 顺序应用变更：
         *          - replace: 删除服务目录下同名目录后从源拷贝
         *          - remove:  仅删除服务目录下对应目录
         *          - add:     从源拷贝对应目录到服务目录
         *          未列出的目录保留不动
         * @param serviceName 服务名称
         * @param sourceDir 新版本根目录
         * @param detail 升级方案
         * @return ResultMsg 操作结果
         */
        ResultMsg ApplyFineGrainedUpgrade(const std::string &serviceName, const std::string &sourceDir,
                                          const UpgradeDetail &detail);

        /**
         * @brief 从细粒度备份回退
         * @details 将 files_backup 下内容恢复到服务目录、nginx_backup 恢复 nginx.conf
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg RollbackFineGrainedUpgrade(const std::string &serviceName);

        // --- 解压目录直接安装 ---

        /**
         * @brief 从已解压目录安装软件包
         * @details 与 InstallSoftwarePackage 等价，softwareDir 应已含 serviceName 子目录
         * @param serviceName 服务名称
         * @param softwareDir 软件包目录（已解压）
         * @return ResultMsg 操作结果
         */
        ResultMsg InstallSoftwarePackageFromDir(const std::string &serviceName, const std::string &softwareDir);

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

        /**
         * @brief 清理服务的所有内容
         * @details 清理服务运行时目录，备份目录、数据目录、.service文件、软链接
         * @param serviceName 服务名称
         */
        void CleanupService(const std::string &serviceName);

        /**
         * @brief 获取 systemd 服务文件前缀
         * @return const char* 前缀，如 "scmd_"
         */
        static constexpr const char* GetServiceFilePrefix() { return ServiceFilePrefix; }

        /**
         * @brief 获取 systemd 服务文件后缀
         * @return const char* 后缀，如 ".service"
         */
        static constexpr const char* GetServiceFileSuffix() { return ServiceFileSuffix; }

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
        static constexpr const char* NginxSystemDefaultSite = "/etc/nginx/sites-enabled/default";  // 系统默认站点
        static constexpr const char* NginxSitesDefaultBackup = "nginx_sites_default";             // 系统默认站点备份名

        /**
         * @brief 禁用系统默认站点，避免与 scm 默认站点冲突
         * @details 将 /etc/nginx/sites-enabled/default 移动到备份目录
         * @return ResultMsg 操作结果
         */
        ResultMsg DisableSystemDefaultSite();

        /**
         * @brief 恢复系统默认站点
         * @details 将备份的 /etc/nginx/sites-enabled/default 恢复
         * @return ResultMsg 操作结果
         */
        ResultMsg RestoreSystemDefaultSite();

        FileDirInfo mCurDirConfig;
    };
}  // namespace qifeng::scm
