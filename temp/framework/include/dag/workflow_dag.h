/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_DAG_WORKFLOW_DAG_H
#define QIFENG_FRAMEWORK_DAG_WORKFLOW_DAG_H

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "dag/dag.h"
#include "dag/node.h"
#include "workflow/WFGraphTask.h"
#include "workflow/WFTaskFactory.h"
#include "workflow/Workflow.h"

namespace qifeng {

    /**
     * @brief 基于 workflow 库的 DAG 节点包装器
     * 包装现有的 DAGNode 使其可在 workflow 的 WFGraphTask 中运行
     */
    class WorkflowDAGNode {
    public:
        /**
         * @brief 构造函数
         * @param node 现有的 DAG 节点指针
         * @param shared_data 共享任务数据
         */
        WorkflowDAGNode(std::shared_ptr<DAGNode> node, std::shared_ptr<CommonTaskData> shared_data);

        /**
         * @brief 获取节点 ID
         */
        const std::string& GetId() const;

        /**
         * @brief 获取节点指针
         */
        std::shared_ptr<DAGNode> GetNode() const;

        /**
         * @brief 获取共享任务数据
         */
        std::shared_ptr<CommonTaskData> GetSharedData() const;

    private:
        std::shared_ptr<DAGNode> mNode;
        std::shared_ptr<CommonTaskData> mSharedData;
    };

    /**
     * @brief 基于 workflow 库的 DAG 执行器
     * 使用 workflow 的 WFGraphTask 来管理和执行 DAG
     */
    class WorkflowDAGExecutor {
    public:
        /**
         * @brief 全局初始化 workflow 库（必须在程序开始时调用一次）
         * @param compute_threads 计算线程数（-1表示自动根据CPU核心数设置）
         */
        static void Init(int compute_threads = -1);

        /**
         * @brief 构造函数
         */
        WorkflowDAGExecutor();
        WorkflowDAGExecutor(const WorkflowDAGExecutor&) = delete;
        WorkflowDAGExecutor& operator=(const WorkflowDAGExecutor&) = delete;
        WorkflowDAGExecutor(WorkflowDAGExecutor&&) = delete;
        WorkflowDAGExecutor& operator=(WorkflowDAGExecutor&&) = delete;

        /**
         * @brief 析构函数
         */
        ~WorkflowDAGExecutor();

        /**
         * @brief 执行 DAG（同步方式）
         * @param dag 要执行的 DAG
         * @return 执行是否成功
         */
        bool Execute(DAG& dag);

        /**
         * @brief 执行 DAG（同步方式），使用指定的共享数据
         * @param dag 要执行的 DAG
         * @param shared_data 共享任务数据
         * @return 执行是否成功
         */
        bool Execute(DAG& dag, const std::shared_ptr<CommonTaskData>& shared_data);

        /**
         * @brief 异步执行 DAG
         * @param dag 要执行的 DAG
         * @param callback 执行完成后的回调函数
         */
        void ExecuteAsync(DAG& dag, std::function<void(bool)> callback);

        /**
         * @brief 异步执行 DAG，使用指定的共享数据
         * @param dag 要执行的 DAG
         * @param shared_data 共享任务数据
         * @param callback 执行完成后的回调函数
         */
        void ExecuteAsync(DAG& dag, const std::shared_ptr<CommonTaskData>& shared_data,
                          std::function<void(bool)> callback);

        /**
         * @brief 停止执行
         */
        void Stop();

        /**
         * @brief 检查是否正在执行
         */
        bool IsRunning() const;

        /**
         * @brief 设置执行队列名称（用于任务分组和隔离）
         * @param queue_name 队列名称
         */
        void SetQueueName(const std::string& queue_name);

        /**
         * @brief 增加一个计算线程
         */
        static bool IncreaseThread();

        /**
         * @brief 减少一个计算线程
         */
        static bool DecreaseThread();

        /**
         * @brief 设置计算线程池大小（运行时动态调整）
         * @param thread_count 目标线程数
         * @return 是否成功
         */
        static bool SetComputeThreadCount(int thread_count);

        /**
         * @brief 获取当前计算线程池大小
         * @return 当前线程数
         */
        static int GetComputeThreadCount();

    private:
        // 记录当前设置的线程数（用于 SetComputeThreadCount）
        static std::atomic<int> mCurrentComputeThreadCount;

    private:
        /**
         * @brief 内部构建 workflow 的 graph task
         * @param dag 要执行的 DAG
         * @param shared_data 共享任务数据
         * @param callback 执行完成后的回调
         * @return 构建好的 WFGraphTask 指针
         */
        WFGraphTask* BuildGraphTask(DAG& dag, const std::shared_ptr<CommonTaskData>& shared_data,
                                    std::function<void(bool)> callback);

        /**
         * @brief 创建一个 Go Task 包装 DAGNode 的执行
         * @param workflow_node WorkflowDAGNode 指针
         * @return WFGoTask 指针
         */
        WFGoTask* CreateNodeTask(WorkflowDAGNode* workflow_node);

        std::atomic<bool> mRunning;
        std::atomic<bool> mStopped;
        std::string mQueueName;
        std::vector<std::unique_ptr<WorkflowDAGNode>> mNodeWrappers;
        std::shared_ptr<CommonTaskData> mSharedData;  // 保存共享数据的引用
        static std::atomic<bool> mInitialized;
    };

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_DAG_WORKFLOW_DAG_H
