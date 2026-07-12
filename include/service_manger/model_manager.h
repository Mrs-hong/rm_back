/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once
#include "common/types.h"
#include <map>
#include <string>
#include <vector>

namespace qifeng::scm {
    class ServiceContext;
    class ServiceManager;

    /**
     * @brief 模型文件管理器
     * @details 从 ServiceManager 中提取的模型管理领域逻辑，负责模型的安装、停用、
     *          回退及备份清理等操作，并维护模型依赖服务的启停与状态恢复。
     *          持有 ServiceContext 引用以访问 ConfigLoader、FileManager 等共享依赖，
     *          持有 ServiceManager 引用以调用服务启停能力。
     */
    class ModelManager {
    public:
        /**
         * @brief 构造函数
         * @param ctx 共享依赖上下文
         * @param serviceManager 服务管理器引用（用于启停依赖模型的服务）
         */
        ModelManager(const ServiceContext &ctx, ServiceManager &serviceManager);

        ~ModelManager();

        ModelManager(const ModelManager &) = delete;
        ModelManager &operator=(const ModelManager &) = delete;
        ModelManager(ModelManager &&) = delete;
        ModelManager &operator=(ModelManager &&) = delete;

        /**
         * @brief 安装/升级模型文件
         * @details 将 srcPath（目录或 tar 包）中的模型安装到 scmd.yaml 配置的 model_dir 下。
         *          自动停止依赖模型的服务，验证运行 10s 无影响后完成，否则回退模型。
         * @param srcPath 模型源路径（目录或 tar/tar.gz 包）
         * @return ResultMsg 操作结果
         */
        ResultMsg AddModel(const std::string &srcPath);

        /**
         * @brief 清除（删除）模型
         * @details 将 model_dir 下指定模型临时重命名为 <name>.back 以便验证回退，验证通过后删除。
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

    private:
        const ServiceContext &mCtx;
        ServiceManager &mServiceManager;
    };
}  // namespace qifeng::scm
