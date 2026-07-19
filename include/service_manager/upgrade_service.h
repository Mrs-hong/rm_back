/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once
#include "common/types.h"
#include <string>

namespace qifeng::scm {
    struct ServiceContext;
    class ServiceManager;
    class ModelManager;
    class NginxManager;
    class DatabaseService;

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
     * @brief 升级服务管理器
     * @details 从 ServiceManager 中提取的升级领域逻辑，负责服务软件包升级、数据库升级、
     *          升级回滚、升级素材查找以及一体化升级（服务+模型+Nginx）编排。
     *          持有 ServiceContext 引用以访问共享依赖，持有 ServiceManager/ModelManager/
     *          NginxManager/DatabaseService 引用以调用各领域操作。
     */
    class UpgradeService {
    public:
        /**
         * @brief 构造函数
         * @param ctx 共享依赖上下文（需由调用方保活）
         * @param serviceManager 服务管理器引用（用于启停服务、文件生成等）
         * @param modelManager 模型管理器引用（用于模型回退/备份清理）
         * @param nginxManager nginx 管理器引用（用于升级期间 nginx 配置切换）
         * @param dbService 数据库服务引用（用于升级期数据库备份/恢复/SQL 执行）
         */
        UpgradeService(const ServiceContext &ctx, ServiceManager &serviceManager, ModelManager &modelManager,
                       NginxManager &nginxManager, DatabaseService &dbService);

        ~UpgradeService();

        UpgradeService(const UpgradeService &) = delete;
        UpgradeService &operator=(const UpgradeService &) = delete;
        UpgradeService(UpgradeService &&) = delete;
        UpgradeService &operator=(UpgradeService &&) = delete;

        /**
         * @brief 更新服务（含升级后备份清理）
         * @details 停止运行中的服务、备份旧版本、安装新版本、恢复运行状态。
         *          升级成功后自动清理旧版本备份。升级过程中数据目录会被保留。
         * @param serviceName 服务名称
         * @param softwareTarPath 新版本服务tar包/目录路径
         * @return ResultMsg 操作结果
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
         * @brief 查找升级素材（服务包/model/nginx）
         * @details srcPath 为空时从服务内部 upgrade.soft_dir 查找；
         *          srcPath 为 tar 包时先解压到临时目录再扫描；
         *          srcPath 为目录时直接扫描。
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

        /**
         * @brief 一体化升级：服务+模型+Nginx
         * @details 完整流程：
         *          1. reset_nginx -w 进入等待页面（所有路由返回404）
         *          1.1 停止服务（若运行中），记录原始运行状态 wasRunning
         *          2. 查找升级素材（服务包/model/nginx），tarDir为空则从服务内部soft_dir查找
         *          3. 若有model素材：add_model（排除当前升级服务，保留.back备份）
         *          4. 若有服务包：UpgradeService，失败则回退模型并恢复nginx
         *          5. 成功收尾：清理模型备份，按nginx素材更新配置或reset_nginx -n恢复
         *          6. 恢复服务运行状态（wasRunning=true时启动），按 upgrade.result_path 写入结果
         * @param serviceName 服务名称
         * @param tarDir 外部素材目录/tar包路径（空则使用服务内部soft_dir）
         * @return ResultMsg 升级结果
         */
        ResultMsg PerformIntegratedUpgrade(const std::string &serviceName, const std::string &tarDir);

    private:
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

        /**
         * @brief 执行升级失败后的回滚操作
         * @details 恢复文件（RollbackSoftwarePackage 或 RollbackFineGrainedUpgrade）、
         *          重新加载配置、重新生成服务文件、必要时回滚数据库
         * @param serviceName 服务名称
         * @param restartIfWasRunning 回滚后是否尝试重新启动服务
         * @param useFineGrained true 表示使用细粒度回滚，false 表示全量回滚
         */
        void DoRollbackUpgrade(const std::string &serviceName, bool restartIfWasRunning, bool useFineGrained = false);

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
        const ServiceContext &mCtx;
        ServiceManager &mServiceManager;
        ModelManager &mModelManager;
        NginxManager &mNginxManager;
        DatabaseService &mDbService;
    };
}  // namespace qifeng::scm
