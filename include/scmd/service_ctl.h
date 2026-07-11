/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once
#include "common/scmd_types.h"
#include "common/types.h"
#include "ipc/data_def.h"
#include "service_manger/service_context.h"

#include <memory>
#include <string>

namespace qifeng::scm {
    class ConfigLoader;
    class ServiceManager;
    class NginxManager;
    class ModelManager;
    class UpgradeService;
    class DatabaseService;

    /**
     * @brief 服务主类：scmd的核心功能入口
     * @details 作为门面类协调 ConfigLoader、ServiceManager、DatabaseInit 三个组件，
     * 提供完整的服务生命周期管理和数据库操作功能。
     * @details nginx/模型/升级领域逻辑已委托给 NginxManager/ModelManager/UpgradeService。
     */
    class ServiceControl {
    public:
        // === 服务生命周期管理 ===

        /**
         * @brief 初始化服务主类
         * @details 加载配置、初始化日志系统、创建ServiceManager、启动已安装且autoStart的服务
         * @return ResultMsg 操作结果
         */
        ResultMsg Init();

        /**
         * @brief 安装服务
         * @details 从tar包安装服务：解压、复制到服务目录、注册配置、生成systemd服务文件
         *  1. 若指定服务为数据库服务则初始化数据库并创建管理员用户
         *  2. 若是其它服务、若依赖数据库则执行数据库初始化脚本
         * @param serviceName 服务名称
         * @param serviceTarPath 服务tar包路径
         * @return ResultMsg 操作结果
         */
        ResultMsg Installed(const std::string &serviceName, const std::string &serviceTarPath);

        /**
         * @brief 卸载服务
         * @details 停止服务、删除服务文件和目录、从配置中移除
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg UninstallService(const std::string &serviceName);

        /**
         * @brief 卸载所有已安装服务（用于UNINSTALL_ALL命令）
         * @details 遍历所有已安装服务逐个卸载，保留 scmd 自身。
         *          卸载顺序按配置中服务序列的逆序，确保依赖关系正确处理。
         * @return ResultMsg 操作结果，部分失败时返回警告
         */
        ResultMsg UninstallAllServices();

        /**
         * @brief 升级服务
         * @details 停止运行中的服务、备份旧版本、安装新版本、恢复运行状态。
         * 升级过程中数据目录会被保留。
         * @param serviceName 服务名称
         * @param serviceTarPath 新版本服务tar包/目录路径
         * @return ResultMsg 操作结果
         */
        ResultMsg UpgradeService(const std::string &serviceName, const std::string &serviceTarPath);

        /**
         * @brief 启动服务
         * @details 通过systemd启动指定服务
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg StartService(const std::string &serviceName);

        /**
         * @brief 停止服务
         * @details 通过systemd停止指定服务
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg StopService(const std::string &serviceName);

        /**
         * @brief 停止所有已安装服务（用于STOP_ALL命令）
         * @details 委托 ServiceManager::StopAllServices，按依赖逆序停止所有服务
         * @return ResultMsg 操作结果
         */
        ResultMsg StopAllServices();

        /**
         * @brief 重启服务
         * @details 通过systemd重启指定服务
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg RestartService(const std::string &serviceName);

        /**
         * @brief 启动 scmd 自身
         * @return ResultMsg 操作结果
         */
        ResultMsg StartScmdSelf();

        /**
         * @brief 停止 scmd 自身
         * @return ResultMsg 操作结果
         */
        ResultMsg StopScmdSelf();

        /**
         * @brief 重启 scmd 自身
         * @return ResultMsg 操作结果
         */
        ResultMsg RestartScmdSelf();

        /**
         * @brief 重新加载服务配置
         * @details 重新加载指定服务的配置并重新生成systemd服务文件
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg ReloadService(const std::string &serviceName);

        /**
         * @brief 获取服务运行状态
         * @details 获取指定服务的运行时状态信息（JSON格式）
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果，成功时msg为JSON格式状态信息
         */
        ResultMsg GetServiceStatus(const std::string &serviceName);

        /**
         * @brief 获取服务运行时结构化信息
         * @details 直接返回 ServiceRuntimeInfo，由调用方决定如何序列化，避免内部 JSON 中转
         * @param serviceName 服务名称
         * @return ServiceRuntimeInfo 服务运行时信息（status 为空表示服务未找到或未初始化）
         */
        ServiceRuntimeInfo GetServiceRuntimeInfo(const std::string &serviceName);

        /**
         * @brief 判断服务是否处于活跃（运行中）状态
         * @details 轻量级查询，仅通过 DBus 获取 systemd ActiveState，不收集完整运行时信息
         * @param serviceName 服务名称
         * @return bool true 表示服务处于 active 状态
         */
        bool IsServiceActive(const std::string &serviceName);

        /**
         * @brief 启用服务开机自启
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg EnableAutoStart(const std::string &serviceName);

        /**
         * @brief 禁用服务开机自启
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg DisableAutoStart(const std::string &serviceName);

        // === 扩展接口（供ScmServer调用） ===

        /**
         * @brief 获取配置加载器
         * @return const ConfigLoader& 配置加载器引用
         */
        const ConfigLoader &GetConfigLoader() const;

        /**
         * @brief 获取所有服务信息（用于LIST命令）
         * @return ResultMsg 操作结果，成功时msg包含服务列表JSON
         */
        ResultMsg GetAllServicesInfo();

        /**
         * @brief 重启所有服务（用于RESTART_ALL命令）
         * @details 按依赖逆序停止所有运行中服务，再按依赖顺序启动
         * @return ResultMsg 操作结果
         */
        ResultMsg RestartAllServices();

        /**
         * @brief 获取操作日志（用于LOG命令）
         * @param logLevel 日志级别过滤
         * @param logCount 日志条数限制
         * @return ResultMsg 操作结果，成功时data包含日志内容
         */
        ResultMsg GetOperationLog(int logLevel, int logCount);

        /**
         * @brief 获取服务的 journal 日志（用于 SLOG 命令）
         * @details 通过 sd-journal API 读取指定服务最近 logCount 条日志，等价于
         *          journalctl -u <unit>.service -n <count>，返回日志原文（每条一行）
         * @param serviceName 服务名称
         * @param logCount 日志行数（<=0 时使用默认 10）
         * @return ResultMsg 成功时 msg 为 journal 日志原文
         */
        ResultMsg GetServiceJournal(const std::string &serviceName, int logCount);

        // === Nginx 配置管理 ===

        /**
         * @brief 独立配置 nginx
         * @details 将指定路径（目录或 tar.gz）中的 nginx 配置安装到系统 nginx 管理目录，
         *          集成到系统 nginx 的 conf.d/snippets 目录
         * @param dirPath nginx 配置源路径（目录或 tar.gz）
         * @return ResultMsg 操作结果
         */
        ResultMsg InitNginx(const std::string &dirPath);

        /**
         * @brief 重置 nginx 配置
         * @details 三种模式：
         *          WAIT: 安装 waiting.conf，所有路由返回 404（服务升级期间）
         *          NORMAL: 移除 waiting.conf，恢复 scm_*.conf 生效
         *          BACK: 移除所有 scm 配置，恢复系统默认欢迎页
         * @param mode 重置模式
         * @return ResultMsg 操作结果
         */
        ResultMsg ResetNginx(NginxResetMode mode);

        // === 模型文件管理 ===

        /**
         * @brief 安装/升级模型文件
         * @details 将 srcPath（目录或 tar 包）中的模型安装到 scmd.yaml 配置的 model_dir 下。
         *          流程：
         *          1. 解析模型名（tar 包以解压后顶层目录名为准，目录以 basename 为准）
         *          2. 停止所有 need_model=true 的服务
         *          3. 备份 model_dir 下同名模型为 <name>.back（若已存在则先删除）
         *          4. 将 srcPath 移动/解压到 model_dir/<name>
         *          5. 启动第 2 步停止的服务，保持运行 10s 验证无影响
         *          6. 恢复各服务起初状态（停止或运行）
         *          7. 任意步骤失败则回退模型并恢复服务状态
         * @param srcPath 模型源路径（目录或 tar/tar.gz 包）
         * @return ResultMsg 操作结果
         */
        ResultMsg AddModel(const std::string &srcPath);

        /**
         * @brief 停用并备份模型
         * @details 将 model_dir 下指定模型重命名为 <name>.back，验证依赖服务无影响后完成。
         *          流程：
         *          1. 停止所有 need_model=true 的服务
         *          2. 将 model_dir/<name> 重命名为 model_dir/<name>.back
         *          3. 启动第 1 步停止的服务，保持运行 10s 验证无影响
         *          4. 恢复各服务起初状态
         *          5. 任意步骤失败则回退模型名并恢复服务状态
         * @param modelName 模型名（model_dir 下的文件或目录名）
         * @return ResultMsg 操作结果
         */
        ResultMsg ClearModel(const std::string &modelName);

        /**
         * @brief 使用服务内部预置升级包执行升级（向后兼容接口）
         * @details 等价于 PerformIntegratedUpgrade(serviceName, "")，从服务内部 soft_dir 查找素材。
         * @param serviceName 服务名称
         * @return ResultMsg 升级结果
         */
        ResultMsg PerformInternalUpgrade(const std::string &serviceName);

        /**
         * @brief 一体化升级：服务+模型+Nginx
         * @details 完整流程：
         *          1. reset_nginx -w 进入等待页面（所有路由返回404）
         *          2. 查找升级素材（服务包/model/nginx），tarDir为空则从服务内部soft_dir查找
         *          3. 若有model素材：add_model（排除当前升级服务，保留.back备份）
         *          4. 若有服务包：UpgradeService，失败则回退模型并恢复nginx
         *          5. 成功收尾：清理模型备份，按nginx素材更新配置或reset_nginx -n恢复
         *          6. 按 upgrade.result_path 写入升级结果文件
         * @param serviceName 服务名称
         * @param tarDir 外部素材目录/tar包路径（空则使用服务内部soft_dir）
         * @return ResultMsg 升级结果
         */
        ResultMsg PerformIntegratedUpgrade(const std::string &serviceName, const std::string &tarDir);

    private:
        /**
         * @brief 清除服务数据
         * @details 尝试删除服务所有关联文件
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg ClearServiceData(const std::string &serviceName);

    private:
        bool mIsInit {false};                             // 是否初始化
        ServiceRuntimeInfo mServiceRuntimeInfo;           // 服务运行时信息
        std::shared_ptr<ConfigLoader> mConfigLoader;      // 配置加载器
        std::shared_ptr<ServiceManager> mServiceManager;  // 服务管理器
        ServiceContext mContext;                          // 共享依赖上下文（需保活以供领域管理器持有引用）
        std::shared_ptr<NginxManager> mNginxManager;      // Nginx 配置管理器
        std::shared_ptr<ModelManager> mModelManager;      // 模型文件管理器
        // 注意：使用 elaborated type specifier（class UpgradeService）避免与同名成员函数 UpgradeService 冲突
        std::shared_ptr<class UpgradeService> mUpgradeService;  // 升级服务管理器
        std::shared_ptr<DatabaseService> mDatabaseService;      // 数据库服务管理器
    };
}  // namespace qifeng::scm
