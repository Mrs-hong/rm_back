/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once
#include "common/scmd_types.h"
#include "common/types.h"
#include "ipc/data_def.h"
#include <memory>
#include <string>
#include <vector>

namespace qifeng::scm {
    class ConfigLoader;
    class FileManager;
    class DBusManager;

    /**
     * @brief 升级素材集合（一体化升级流程使用）
     */
    struct UpgradeArtifacts {
        std::string servicePackage;  // 服务软件包路径（空表示无）
        std::string modelPath;       // 模型素材路径（空表示无）
        std::string nginxDir;        // nginx配置目录路径（空表示无）
        std::string tempDir;         // tar解压临时目录（空表示无需清理）
    };

    /**
     * @brief 服务管理器
     *
     * 负责协调 ConfigLoader、FileManager、DBusManager 三个组件，
     * 提供完整的服务生命周期管理功能，包括安装、启动、停止、重启、更新、卸载等操作。
     */
    class ServiceManager {
    public:
        /**
         * @brief 构造函数
         * @param configLoader 配置加载器智能指针
         */
        explicit ServiceManager(std::shared_ptr<ConfigLoader> configLoader);

        /**
         * @brief 析构函数
         */
        ~ServiceManager();

        ServiceManager(const ServiceManager &) = delete;
        ServiceManager &operator=(const ServiceManager &) = delete;
        ServiceManager(ServiceManager &&) = delete;
        ServiceManager &operator=(ServiceManager &&) = delete;

        /**
         * @brief 安装服务
         *
         * @param softwareTarPath 软件tar包路径
         * @param serviceName 服务名称（可选，默认从tar包中读取）
         * @return ResultMsg 成功时包含实际服务名称
         */
        ResultMsg InstallService(const std::string &softwareTarPath, const std::string &serviceName);

        /**
         * @brief 启动服务
         *
         * @param serviceName 服务名称
         * @return ResultMsg
         */
        ResultMsg StartService(const std::string &serviceName);

        /**
         * @brief 停止服务
         *
         * @param serviceName 服务名称
         * @return ResultMsg
         */
        ResultMsg StopService(const std::string &serviceName);

        /**
         * @brief 重启服务
         *
         * @param serviceName 服务名称
         * @return ResultMsg
         */
        ResultMsg RestartService(const std::string &serviceName);

        /**
         * @brief 启动 scmd 自身（qifeng-scmd.service）
         * @details scmd 正在处理请求即说明自身已在运行，直接返回成功
         * @return ResultMsg 操作结果
         */
        ResultMsg StartScmdSelf();

        /**
         * @brief 停止 scmd 自身（qifeng-scmd.service）
         * @details 通过 systemd DBus 停止自身守护进程
         * @return ResultMsg 操作结果
         */
        ResultMsg StopScmdSelf();

        /**
         * @brief 重启 scmd 自身（qifeng-scmd.service）
         * @details 通过 systemd DBus 重启自身守护进程
         * @return ResultMsg 操作结果
         */
        ResultMsg RestartScmdSelf();

        /**
         * @brief 重新加载服务配置
         *
         * @param serviceName 服务名称（可选，空字符串表示重载所有服务）
         * @return ResultMsg
         */
        ResultMsg ReloadService(const std::string &serviceName);

        /**
         * @brief 获取服务运行时信息
         *
         * @param serviceName 服务名称
         * @return ResultMsg 成功时包含 ServiceRuntimeInfo 的序列化字符串
         */
        ResultMsg GetServiceStatus(const std::string &serviceName);

        /**
         * @brief 获取服务运行时详细信息
         *
         * @param serviceName 服务名称
         * @return ServiceRuntimeInfo 服务运行时信息
         */
        ServiceRuntimeInfo GetServiceRuntimeInfo(const std::string &serviceName);

        /**
         * @brief 判断服务是否处于活跃（运行中）状态
         * @details 仅通过 DBus 查询 systemd ActiveState，轻量级，不收集完整运行时信息
         * @param serviceName 服务名称
         * @return bool true 表示服务处于 active 状态
         */
        bool IsServiceActive(const std::string &serviceName);

        /**
         * @brief 卸载服务
         *
         * @param serviceName 服务名称
         * @return ResultMsg
         */
        ResultMsg UninstallService(const std::string &serviceName);

        /**
         * @brief 清除服务名可能存在的数据
         *
         * @param serviceName 服务名称
         * @return ResultMsg
         */
        ResultMsg ClearServiceData(const std::string &serviceName);

        /**
         * @brief 更新服务
         *
         * @param serviceName 服务名称
         * @param softwareTarPath 新软件tar包路径
         * @return ResultMsg
         */
        ResultMsg UpdateService(const std::string &serviceName, const std::string &softwareTarPath);

        /**
         * @brief 清理升级备份
         * @details 在确认升级完全成功后调用，删除旧版本备份
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg CleanUpgradeBackup(const std::string &serviceName);

        /**
         * @brief 启用服务开机自启
         *
         * @param serviceName 服务名称
         * @return ResultMsg
         */
        ResultMsg EnableAutoStart(const std::string &serviceName);

        /**
         * @brief 禁用服务开机自启
         *
         * @param serviceName 服务名称
         * @return ResultMsg
         */
        ResultMsg DisableAutoStart(const std::string &serviceName);

        /**
         * @brief 按依赖顺序启动所有 isAutoStart 的服务
         * @return ResultMsg 操作结果
         */
        ResultMsg StartAllAutoStartServices();

        /**
         * @brief 按依赖逆序停止所有运行中的服务
         * @return ResultMsg 操作结果
         */
        ResultMsg StopAllServices();

        /**
         * @brief 获取当前启动/停止序列
         * @return ServiceSequence 序列
         */
        ServiceSequence GetServiceSequence();

        // === Nginx 配置管理 ===

        /**
         * @brief 独立配置 nginx
         * @details 将指定路径（目录或 tar.gz）中的 nginx 配置安装到系统 nginx 管理目录，
         *          集成到系统 nginx 的 conf.d/snippets 目录，使静态文件和反向代理配置生效。
         *          配置测试失败时自动回退。
         * @param dirPath nginx 配置源路径（目录或 tar.gz）
         * @return ResultMsg 操作结果
         */
        ResultMsg InitNginx(const std::string &dirPath);

        /**
         * @brief 重置 nginx 配置
         * @details 三种模式：
         *          WAIT: 安装 waiting.conf，所有路由返回 404（服务升级期间）
         *          NORMAL: 移除 waiting.conf，恢复 scm_*.conf 生效
         *          BACK: 移除所有 scm 配置和 nginx 目录，恢复系统默认欢迎页
         * @param mode 重置模式
         * @return ResultMsg 操作结果
         */
        ResultMsg ResetNginx(NginxResetMode mode);

        // === 模型文件管理 ===

        /**
         * @brief 安装/升级模型文件
         * @details 将 srcPath（目录或 tar 包）中的模型安装到 scmd.yaml 配置的 model_dir 下。
         *          自动停止依赖模型的服务，验证运行 10s 无影响后完成，否则回退模型。
         * @param srcPath 模型源路径（目录或 tar/tar.gz 包）
         * @return ResultMsg 操作结果
         */
        ResultMsg AddModel(const std::string &srcPath);

        /**
         * @brief 停用并备份模型
         * @details 将 model_dir 下指定模型重命名为 <name>.back，验证依赖服务无影响后完成。
         * @param modelName 模型名（model_dir 下的文件或目录名）
         * @return ResultMsg 操作结果
         */
        ResultMsg ClearModel(const std::string &modelName);

        /**
         * @brief 安装模型并保留备份（用于一体化升级流程）
         * @details 与 AddModel 流程一致，但：
         *          1. 排除 excludeService（升级中的服务已停止，无需在此停止/启动）
         *          2. 成功后不清理 .back 备份，由调用方确认升级成功后调用 CleanModelBackup 清理
         * @param srcPath 模型源路径（目录或 tar/tar.gz 包）
         * @param excludeService 排除的服务名（不参与停止/启动/验证）
         * @param[out] modelName 输出安装的模型名
         * @return ResultMsg 操作结果
         */
        ResultMsg AddModelWithBackupRetained(const std::string &srcPath, const std::string &excludeService,
                                              std::string &modelName);

        /**
         * @brief 清理模型备份（一体化升级成功后调用）
         * @param modelName 模型名
         * @return ResultMsg 操作结果
         */
        ResultMsg CleanModelBackup(const std::string &modelName);

        /**
         * @brief 回退模型（一体化升级失败时调用）
         * @details 删除新安装的模型，若存在 .back 备份则恢复
         * @param modelName 模型名
         * @return ResultMsg 操作结果
         */
        ResultMsg RollbackModel(const std::string &modelName);

        /**
         * @brief 查找升级素材（服务包/model/nginx）
         * @details srcPath 为空时从服务内部 upgrade.soft_dir 查找；
         *          srcPath 为 tar 包时先解压到临时目录再扫描；
         *          srcPath 为目录时直接扫描。
         *          扫描规则：
         *          - 服务包：<serviceName>*.tar.gz（前缀匹配，取排序后第一个）
         *          - 模型素材：model* 前缀的目录或 tar 包（取第一个）
         *          - nginx 素材：nginx* 前缀的目录（取第一个）
         * @param serviceName 服务名称（用于服务包前缀匹配）
         * @param srcPath 素材源路径（目录或tar包）；空则使用服务内部soft_dir
         * @param[out] artifacts 输出查找到的素材集合
         * @return ResultMsg 操作结果
         */
        ResultMsg FindUpgradeArtifacts(const std::string &serviceName, const std::string &srcPath,
                                        UpgradeArtifacts &artifacts);

        /**
         * @brief 清理素材的临时解压目录（一体化升级流程结束后调用）
         * @param artifacts 升级素材集合，清理其中的 tempDir
         */
        void CleanupArtifactsTempDir(UpgradeArtifacts &artifacts);

        /**
         * @brief 从服务内部预置目录查找升级包
         * @details 读取服务 upgrade.soft_dir 配置，基于 currentServiceDir 解析为绝对路径，
         *          在该目录下查找第一个 .tar.gz 升级包（排序后取第一个保证确定性）。
         *          包内服务名校验和版本递增校验由 UpdateService 内部完成。
         * @param serviceName 服务名称
         * @return ResultMsg 成功时 msg 为升级包绝对路径，失败时 msg 为错误原因
         */
        ResultMsg FindInternalUpgradePackage(const std::string &serviceName);

        /**
         * @brief 从升级包中解析新版本号（不执行解压安装）
         * @details 通过 tar 命令流式读取包内 <serviceName>/service.yaml 并提取 version 字段，
         *          避免为读取版本号做完整解压。要求运行环境存在 tar 命令。
         * @param serviceName 服务名称
         * @param packagePath 升级包路径（.tar.gz）
         * @return std::string 版本号，解析失败返回空字符串
         */
        std::string ReadVersionFromPackage(const std::string &serviceName, const std::string &packagePath);

    private:
        /**
         * @brief 解压软件tar包到指定目录并可选指定解压后的文件名
         * @param tarPath tar包路径
         * @param extractDir 输出参数，解压目录路径
         * @param newName 可选，指定解压后的目录名，默认为空字符串表示不处理
         * @return ResultMsg 操作结果
         */
        ResultMsg ExtractSoftwareTar(const std::string &tarPath, const std::string &extractDir,
                                     const std::string &newName = "");

        /**
         * @brief 检查服务依赖关系
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg CheckServiceDependencies(const std::string &serviceName);

        /**
         * @brief 启动依赖服务
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg StartDependentServices(const std::string &serviceName);

        /**
         * @brief 设置服务运行用户
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg SetServiceUser(const std::string &serviceName);

        /**
         * @brief 生成并创建 systemd service 文件
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg GenerateAndCreateServiceFile(const std::string &serviceName);

        /**
         * @brief 收集服务运行时信息
         * @param serviceName 服务名称
         * @return ServiceRuntimeInfo 运行时信息
         */
        ServiceRuntimeInfo CollectRuntimeInfo(const std::string &serviceName);

        /**
         * @brief 将 systemd ActiveState 转换为内部 ServiceStatus
         * @param activeState systemd 状态字符串
         * @return ServiceStatus 转换后的状态
         */
        ServiceStatus ConvertActiveStateToStatus(const std::string &activeState);

        /**
         * @brief 清理临时目录
         * @param dirPath 目录路径
         */
        void CleanupTempDirectory(const std::string &dirPath);

        /**
         * @brief 初始化文件管理器
         * @return ResultMsg 操作结果
         */
        ResultMsg InitializeFileManager();

        /**
         * @brief 初始化 DBus 管理器
         * @return ResultMsg 操作结果
         */
        ResultMsg InitializeDBusManager();

        /**
         * @brief 将服务名转换为 systemd 单元名（添加 scmd_ 前缀）
         * @param serviceName 服务名称
         * @return std::string systemd 单元名
         */
        static std::string ToSystemdUnitName(const std::string &serviceName);

        /**
         * @brief 解析服务名（从指定 service.yaml 路径读取）
         * @param yamlPath service.yaml 完整路径
         * @return std::string 解析后的服务名称，失败返回空字符串
         */
        std::string ResolveServiceName(const std::string &yamlPath);

        /**
         * @brief 标记启动序列需要重新计算
         */
        void MarkSequenceDirty();

        /**
         * @brief 确保启动序列是最新的（如果 dirty 则重新计算）
         * @return ResultMsg 操作结果
         */
        ResultMsg EnsureSequenceUpdated();

        /**
         * @brief 获取依赖指定服务的所有服务（反向依赖）
         * @param serviceName 服务名称
         * @return std::vector<std::string> 依赖此服务的服务列表
         */
        std::vector<std::string> GetDependentServices(const std::string &serviceName);

        /**
         * @brief 执行升级失败后的回滚操作
         * @details 恢复文件（RollbackSoftwarePackage 或 RollbackFineGrainedUpgrade）、
         *          重新加载配置、重新生成服务文件、必要时回滚数据库与 nginx 配置
         * @param serviceName 服务名称
         * @param restartIfWasRunning 回滚后是否尝试重新启动服务
         * @param useFineGrained true 表示使用细粒度回滚，false 表示全量回滚
         */
        void DoRollbackUpgrade(const std::string &serviceName, bool restartIfWasRunning, bool useFineGrained = false);

        /**
         * @brief 升级后启动服务并按 keep_alive_time_sec 验证持续运行，结束后恢复原状态
         * @details 无论升级前服务是否运行，只要 keep_alive_time_sec > 0 就启动并等待验证时长：
         *          - 验证通过：若升级前未运行（wasRunning=false），则停止服务恢复原状态
         *          - 验证失败：调用 DoRollbackUpgrade 回滚，并返回错误
         * @param serviceName 服务名称
         * @param wasRunning 升级前服务是否在运行
         * @param useFineGrained 回滚时是否使用细粒度回滚
         * @return ResultMsg 验证成功返回 success，失败返回错误信息
         */
        ResultMsg VerifyAndRestoreServiceState(const std::string &serviceName, bool wasRunning, bool useFineGrained);

        // --- 模型管理辅助 ---

        /**
         * @brief 获取所有 need_model=true 的服务名
         * @return std::vector<std::string> 依赖模型的服务列表
         */
        std::vector<std::string> GetModelDependentServices();

        /**
         * @brief 获取所有 need_model=true 的服务名（排除指定服务）
         * @param excludeService 需排除的服务名（如正在升级的服务）
         * @return std::vector<std::string> 依赖模型的服务列表
         */
        std::vector<std::string> GetModelDependentServices(const std::string &excludeService);

        /**
         * @brief 停止指定服务列表，记录每个服务停止前的运行状态
         * @param serviceNames 服务名列表
         * @param preStates 输出参数，按服务名记录停止前是否在运行
         * @return ResultMsg 操作结果
         */
        ResultMsg StopServicesWithStateRecord(const std::vector<std::string> &serviceNames,
                                               std::map<std::string, bool> &preStates);

        /**
         * @brief 按停止前状态恢复服务（原本运行的启动，原本停止的保持停止）
         * @param preStates 服务名 -> 停止前是否运行
         * @return ResultMsg 操作结果
         */
        ResultMsg RestoreServicesByState(const std::map<std::string, bool> &preStates);

        /**
         * @brief 启动指定服务列表并按各自 keep_alive_time_sec 验证持续运行
         * @details 每个服务启动后等待其 keepAliveTimeSec 秒再检查活跃状态；
         *          keepAliveTimeSec 为 0 则跳过该服务的验证。
         * @param serviceNames 服务名列表
         * @return ResultMsg 全部启动并验证通过返回成功，否则返回失败原因
         */
        ResultMsg StartServicesAndWaitRunning(const std::vector<std::string> &serviceNames);

        /**
         * @brief 校验模型名合法（非空、不以 .back 结尾）
         * @param modelName 模型名
         * @return ResultMsg 合法返回成功
         */
        ResultMsg ValidateModelName(const std::string &modelName) const;

        // --- 升级辅助 ---

        /**
         * @brief 判断升级输入是 tar 包还是已解压目录
         * @param path 输入路径
         * @return bool true 表示是 .tar.gz 包，false 表示目录
         */
        bool IsTarPackage(const std::string &path) const;

        /**
         * @brief 准备升级源目录（统一将 tar 解压或目录拷贝到 tempDir）
         * @param serviceName 服务名称
         * @param softwareTarPath 输入路径（tar 包或目录）
         * @param tempDir 临时目录
         * @return ResultMsg 操作结果
         */
        ResultMsg PrepareUpgradeSource(const std::string &serviceName, const std::string &softwareTarPath,
                                       const std::string &tempDir);

        /**
         * @brief 备份数据库（升级前调用）
         * @details 当服务依赖 mariadb 且 service.yaml 含 initDB_sql_dir 时，
         *          备份服务用户拥有的所有数据库到 backupDir/<serviceName>/db_backup/
         * @param serviceName 服务名称
         * @return ResultMsg 成功时 msg 为备份目录路径；无需备份时 msg 为空
         */
        ResultMsg BackupDatabaseIfNeeded(const std::string &serviceName);

        /**
         * @brief 执行升级用 SQL 脚本
         * @details 当 service.yaml 含 initDB_sql_dir 时，按字母序执行该目录下所有 .sql 脚本
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg ExecuteDatabaseUpgradeScripts(const std::string &serviceName);

        /**
         * @brief 回退数据库
         * @details 从 backupDir/<serviceName>/db_backup/ 恢复数据库
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg RollbackDatabase(const std::string &serviceName);

        /**
         * @brief 细粒度升级（基于 up_detail.yaml）
         * @details 按以下顺序执行：
         *          停止 nginx → DB 备份 → 文件备份 → 文件升级 → 重新加载配置 → 执行SQL → 启动服务
         *          任一步失败调用 DoRollbackUpgrade 回退
         * @param serviceName 服务名称
         * @param sourceDir 新版本根目录
         * @param upDetailPath up_detail.yaml 路径
         * @param wasRunning 升级前是否运行中
         * @return ResultMsg 操作结果
         */
        ResultMsg UpdateServiceWithDetail(const std::string &serviceName, const std::string &sourceDir,
                                          const std::string &upDetailPath, bool wasRunning);

        /**
         * @brief 默认全量升级（无 up_detail.yaml 时调用）
         * @details 保持现有全量备份+覆盖行为，保留 dataDir 迁移
         * @param serviceName 服务名称
         * @param parentDir 临时目录（包含 <serviceName>/ 子目录），与 InstallSoftwarePackage 路径约定一致
         * @param wasRunning 升级前是否运行中
         * @return ResultMsg 操作结果
         */
        ResultMsg UpdateServiceDefault(const std::string &serviceName, const std::string &parentDir, bool wasRunning);

    private:
        std::shared_ptr<ConfigLoader> mConfigLoader;
        std::unique_ptr<FileManager> mFileManager;
        std::unique_ptr<DBusManager> mDBusManager;
        ServiceSequence mServiceSequence;  // 缓存的启动/停止序列
        bool mInitialized;
        bool mSequenceDirty;  // 序列是否需要重新计算
    };
}  // namespace qifeng::scm
