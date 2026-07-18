/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once
#include "common/scmd_types.h"
#include "common/types.h"
#include "ipc/data_def.h"

#include <memory>
#include <string>

namespace qifeng::scm {
    class ConfigLoader;
    class ServiceManager;

    /**
     * @brief 服务主类：scmd的核心功能入口
     * @details 作为门面类协调 ConfigLoader、ServiceManager、DatabaseInit 三个组件，
     * 提供完整的服务生命周期管理和数据库操作功能。
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

        // === 数据库操作 ===

        /**
         * @brief 创建数据库用户
         * @details 在指定服务的数据库中创建新用户，需在数据库启动后调用
         * @param username 用户名
         * @param password 密码
         * @return ResultMsg 操作结果
         */
        ResultMsg CreateDatabaseUser(const DatabaseType &dbType, const std::string &username,
                                     const std::string &password);

        /**
         * @brief 删除数据库用户
         * @details 在指定服务的数据库中删除指定用户及所有权限数据库，需在数据库启动后调用。
         *          提供密码时可通过用户身份连接发现更多数据库（包括无显式 GRANT 的库）。
         * @param dbType 数据库类型
         * @param username 用户名
         * @param password 用户密码（可选，提供时可发现更多数据库）
         * @return ResultMsg 操作结果
         */
        ResultMsg DeleteDatabaseUser(const DatabaseType &dbType, const std::string &username,
                                     const std::string &password = "");

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

        /**
         * @brief 获取服务日志（新需求3.1：优先读日志文件，回退 journal）
         * @details 读取顺序：
         *   1. 优先读取服务日志文件 <logsDir>/<serviceName>/<serviceName>.log 的最后 logCount 行
         *   2. 文件不存在或为空时，回退读取 systemd journal
         * @param serviceName 服务名称（空表示 scmd 自身，读取 <logsDir>/qifeng-scm/qifeng-scm.log，
         *                     journal 回退时使用 unit "qifeng-scmd"）
         * @param logCount 日志行数（<=0 时使用默认 10）
         * @return ResultMsg 成功时 msg 为日志内容
         */
        ResultMsg GetServiceLog(const std::string &serviceName, int logCount);

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
         *          1.1 停止服务（若运行中），记录原始运行状态 wasRunning
         *          2. 查找升级素材（服务包/model/nginx），tarDir为空则从服务内部soft_dir查找
         *          3. 若有model素材：add_model（排除当前升级服务，保留.back备份）
         *          4. 若有服务包：UpgradeService，失败则回退模型并恢复nginx
         *          5. 成功收尾：清理模型备份，按nginx素材更新配置或reset_nginx -n恢复
         *          6. 恢复服务运行状态（wasRunning=true时启动），按 upgrade.result_path 写入结果
         *
         *          服务停启语义：服务在 nginx 进入等待页面后立即停止（Step 1.1），
         *          在所有退出路径（素材查找失败、模型安装失败、服务升级失败、nginx更新失败、
         *          成功收尾）上根据 wasRunning 恢复服务运行状态。
         * @param serviceName 服务名称
         * @param tarDir 外部素材目录/tar包路径（空则使用服务内部soft_dir）
         * @return ResultMsg 升级结果
         */
        ResultMsg PerformIntegratedUpgrade(const std::string &serviceName, const std::string &tarDir);

    private:
        /**
         * @brief 初始化服务的数据库
         * @details 为服务执行数据库初始化脚本
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg InitServiceDatabase(const std::string &serviceName);

        std::string GetDependentDatabaseServiceName(const std::string &serviceName);

        /**
         * @brief 写入数据库用户密码到文件
         * @details 用于后续服务启动时验证数据库连接
         * @param serviceDefinition 服务定义
         * @param dbUserName 数据库用户名
         * @param dbPassword 数据库密码
         * @return ResultMsg 操作结果
         */
        ResultMsg CreateDbUserPassword(const ServiceDefinition &serviceDefinition, const std::string &dbUserName,
                                       const std::string &dbPassword);

        /**
         * @brief 清除数据库用户密码文件
         * @details 用于在服务卸载时删除数据库用户密码文件
         * @param serviceDefinition 服务定义
         * @return ResultMsg 操作结果
         */
        ResultMsg ClearDbUserPasswordFile(const ServiceDefinition &serviceDefinition);

        /**
         * @brief 为服务执行数据库初始化脚本
         * @details 从指定目录执行所有SQL脚本
         * @param dbServiceName 数据库服务名称
         * @param sqlDir SQL初始化脚本目录路径
         * @param dbUserName 数据库用户名
         * @param dbPassword 数据库密码
         * @return ResultMsg 操作结果
         */
        ResultMsg ExecuteDbInitScripts(const DatabaseType &dbType, const std::string &sqlDir,
                                       const std::string &dbUserName = "", const std::string &dbPassword = "");

        /**
         * @brief 清除服务数据
         * @details 尝试删除服务所有关联文件
         * @param serviceName 服务名称
         * @return ResultMsg 操作结果
         */
        ResultMsg ClearServiceData(const std::string &serviceName);

        /**
         * @brief 清除数据库数据
         * @details 尝试删除服务所有关联数据库用户、数据库等。
         *          从服务密码文件中读取用户密码，以用户身份发现所有可访问数据库。
         * @param svc 服务定义（需包含 dbInfo 和 currentServiceDir）
         * @return ResultMsg 操作结果
         */
        ResultMsg ClearDatabaseData(const ServiceDefinition &svc);

        /**
         * @brief 预创建服务日志目录（新需求3.1）
         * @details 创建 <logsDir>/qifeng-scm/ 和每个已注册服务的 <logsDir>/<serviceName>/ 目录，
         *          确保 systemd StandardOutput=append: 能正常写入。systemd 不会自动创建父目录。
         */
        void CreateServiceLogDirs();

        /**
         * @brief 将服务最近 journal 记录同步追加到服务日志文件（新需求3.1）
         * @details 在 Start/Stop/Restart 后调用，将 systemd 自身的操作记录
         *          （如 "Started xxx"、"Stopped xxx"）追加到 <logsDir>/<serviceName>/<serviceName>.log，
         *          与 StandardOutput 重定向的服务进程输出汇聚在同一文件。
         * @param serviceName 服务名称（空或未注册时直接返回）
         */
        void SyncJournalToServiceLog(const std::string &serviceName);

    private:
        bool mIsInit {false};                             // 是否初始化
        ServiceRuntimeInfo mServiceRuntimeInfo;           // 服务运行时信息
        std::shared_ptr<ConfigLoader> mConfigLoader;      // 配置加载器
        std::shared_ptr<ServiceManager> mServiceManager;  // 服务管理器
    };
}  // namespace qifeng::scm
