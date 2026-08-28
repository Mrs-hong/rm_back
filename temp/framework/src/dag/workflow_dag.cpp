/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/logger.h"
#include "dag/workflow_dag.h"
#include "workflow/WFGlobal.h"
#include <cstdio>

namespace qifeng {

    // 静态成员变量的定义
    std::atomic<bool> WorkflowDAGExecutor::mInitialized(false);
    std::atomic<int> WorkflowDAGExecutor::mCurrentComputeThreadCount(0);

    // WorkflowDAGNode 实现
    WorkflowDAGNode::WorkflowDAGNode(std::shared_ptr<DAGNode> node, std::shared_ptr<CommonTaskData> shared_data)
        : mNode(std::move(node)), mSharedData(std::move(shared_data)) {
    }

    const std::string& WorkflowDAGNode::GetId() const {
        return mNode->GetId();
    }

    std::shared_ptr<DAGNode> WorkflowDAGNode::GetNode() const {
        return mNode;
    }

    std::shared_ptr<CommonTaskData> WorkflowDAGNode::GetSharedData() const {
        return mSharedData;
    }

    // WorkflowDAGExecutor 实现
    void WorkflowDAGExecutor::Init(int compute_threads) {
        if (mInitialized.exchange(true)) {
            SLOG_WARN << "WorkflowDAGExecutor already initialized";
            return;
        }

        struct WFGlobalSettings settings = GLOBAL_SETTINGS_DEFAULT;
        settings.compute_threads = compute_threads;
        WORKFLOW_library_init(&settings);

        // 记录初始线程数
        if (compute_threads == -1) {
            mCurrentComputeThreadCount.store((static_cast<int>(std::thread::hardware_concurrency())));
            if (mCurrentComputeThreadCount.load() <= 0) {
                mCurrentComputeThreadCount.store(4);
            }
        } else {
            mCurrentComputeThreadCount.store(compute_threads);
        }

        SLOG_INFO << "WorkflowDAGExecutor initialized: compute_threads=" << mCurrentComputeThreadCount.load();
    }

    bool WorkflowDAGExecutor::SetComputeThreadCount(int thread_count) {
        if (thread_count <= 0) {
            SLOG_WARN << "Invalid thread count: " << thread_count;
            return false;
        }

        int currentCount = mCurrentComputeThreadCount.load();
        if (currentCount == thread_count) {
            SLOG_INFO << "Thread count is already " << thread_count;
            return true;
        }

        SLOG_INFO << "Changing compute thread count from " << currentCount << " to " << thread_count;

        bool success = true;
        if (thread_count > currentCount) {
            int diff = thread_count - currentCount;
            for (int i = 0; i < diff; i++) {
                if (!WFGlobal::increase_compute_thread()) {
                    success = false;
                    SLOG_ERROR << "Failed to increase compute thread " << (i + 1);
                    break;
                }
            }
        } else {
            int diff = currentCount - thread_count;
            for (int i = 0; i < diff; i++) {
                if (!WFGlobal::decrease_compute_thread()) {
                    success = false;
                    SLOG_ERROR << "Failed to decrease compute thread " << (i + 1);
                    break;
                }
            }
        }

        if (success) {
            mCurrentComputeThreadCount.store(thread_count);
            SLOG_INFO << "Successfully changed compute thread count to " << thread_count;
        }

        return success;
    }

    int WorkflowDAGExecutor::GetComputeThreadCount() {
        return mCurrentComputeThreadCount.load();
    }

    WorkflowDAGExecutor::WorkflowDAGExecutor() : mRunning(false), mStopped(false), mQueueName("") {
    }

    WorkflowDAGExecutor::~WorkflowDAGExecutor() {
        Stop();
    }

    void WorkflowDAGExecutor::SetQueueName(const std::string& queue_name) {
        mQueueName = queue_name;
    }

    bool WorkflowDAGExecutor::IncreaseThread() {
        return WFGlobal::increase_compute_thread();
    }

    bool WorkflowDAGExecutor::DecreaseThread() {
        return WFGlobal::decrease_compute_thread();
    }

    bool WorkflowDAGExecutor::Execute(DAG& dag) {
        return Execute(dag, nullptr);
    }

    bool WorkflowDAGExecutor::Execute(DAG& dag, const std::shared_ptr<CommonTaskData>& shared_data) {
        if (mRunning.load()) {
            SLOG_WARN << "Executor is already running";
            return false;
        }

        if (!dag.Validate()) {
            SLOG_ERROR << "DAG validation failed, contains cycles";
            return false;
        }

        dag.Reset();

        std::atomic<bool> completed(false);
        bool success = false;

        auto callback = [&completed, &success](bool result) {
            success = result;
            completed.store(true);
        };

        auto graphTask = BuildGraphTask(dag, shared_data, callback);
        graphTask->start();

        while (!completed.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (mStopped.load()) {
                SLOG_INFO << "DAG execution stopped";
                return false;
            }
        }

        return success;
    }

    void WorkflowDAGExecutor::ExecuteAsync(DAG& dag, std::function<void(bool)> callback) {
        ExecuteAsync(dag, nullptr, std::move(callback));
    }

    void WorkflowDAGExecutor::ExecuteAsync(DAG& dag, const std::shared_ptr<CommonTaskData>& shared_data,
                                           std::function<void(bool)> callback) {
        if (mRunning.load()) {
            SLOG_WARN << "Executor is already running, callback will not be called";
            if (callback) {
                callback(false);
            }
            return;
        }

        if (!dag.Validate()) {
            SLOG_ERROR << "DAG validation failed, contains cycles";
            if (callback) {
                callback(false);
            }
            return;
        }

        dag.Reset();

        auto graphTask = BuildGraphTask(dag, shared_data, std::move(callback));
        graphTask->start();
    }

    void WorkflowDAGExecutor::Stop() {
        mStopped.store(true);
        mRunning.store(false);
        // 设置共享数据的取消标志，让正在执行的节点感知到
        if (mSharedData) {
            mSharedData->Cancel();
            SLOG_INFO << "Cancelling shared data";
        }
        // 不直接调用 dismiss()，避免 workflow 框架的断言失败
        SLOG_INFO << "Stopping workflow DAG executor";
    }

    bool WorkflowDAGExecutor::IsRunning() const {
        return mRunning.load();
    }

    WFGraphTask* WorkflowDAGExecutor::BuildGraphTask(DAG& dag, const std::shared_ptr<CommonTaskData>& shared_data,
                                                     std::function<void(bool)> callback) {
        mNodeWrappers.clear();
        auto data = shared_data ? shared_data : std::make_shared<CommonTaskData>();
        mSharedData = data;  // 保存共享数据的引用
        auto graphCallback = [this, callback](WFGraphTask* task) {
            bool success = (task->get_state() == WFT_STATE_SUCCESS);
            mRunning.store(false);
            mSharedData.reset();  // 清除共享数据引用
            SLOG_INFO << "DAG execution " << (success ? "succeeded" : "failed");
            if (callback) {
                callback(success);
            }
            mNodeWrappers.clear();
        };
        WFGraphTask* graphTask = WFTaskFactory::create_graph_task(graphCallback);
        const auto& allNodes = dag.GetAllNodes();
        std::unordered_map<std::string, WFGraphNode*> graphNodeMap;
        for (const auto& pair : allNodes) {
            const auto& nodeId = pair.first;
            const auto& dagNode = pair.second;
            auto wrapper = std::make_unique<WorkflowDAGNode>(dagNode, data);
            WorkflowDAGNode* wrapperPtr = wrapper.get();
            mNodeWrappers.push_back(std::move(wrapper));
            WFGoTask* goTask = CreateNodeTask(wrapperPtr);
            WFGraphNode& graphNode = graphTask->create_graph_node(goTask);
            graphNodeMap[nodeId] = &graphNode;
        }
        for (const auto& pair : allNodes) {
            const auto& nodeId = pair.first;
            const auto& dagNode = pair.second;

            auto graphNodeIt = graphNodeMap.find(nodeId);
            if (graphNodeIt == graphNodeMap.end()) {
                continue;
            }
            WFGraphNode* currentGraphNode = graphNodeIt->second;

            for (const auto& dependency : dagNode->GetDependencies()) {
                const std::string& depId = dependency->GetId();
                auto depGraphNodeIt = graphNodeMap.find(depId);
                if (depGraphNodeIt != graphNodeMap.end()) {
                    WFGraphNode* depGraphNode = depGraphNodeIt->second;
                    depGraphNode->precede(*currentGraphNode);
                }
            }
        }
        mRunning.store(true);
        mStopped.store(false);
        return graphTask;
    }

    WFGoTask* WorkflowDAGExecutor::CreateNodeTask(WorkflowDAGNode* workflow_node) {
        auto executeFunc = [workflow_node]() {
            auto node = workflow_node->GetNode();
            auto sharedData = workflow_node->GetSharedData();

            SLOG_DEBUG << "Executing node: " << node->GetId();

            node->SetState(DAGNodeState::RUNNING);

            bool success = false;
            try {
                success = node->Execute(sharedData);
            } catch (const std::exception& e) {
                SLOG_ERROR << "Node " << node->GetId() << " threw exception: " << e.what();
                node->SetError(std::string("Exception: ") + e.what());
                success = false;
            }

            if (success) {
                SLOG_DEBUG << "Node completed: " << node->GetId();
                node->SetState(DAGNodeState::COMPLETED);
            } else {
                SLOG_ERROR << "Node failed: " << node->GetId() << ", error: " << node->GetError();
                node->SetState(DAGNodeState::FAILED);
            }
        };

        return WFTaskFactory::create_go_task(mQueueName, std::move(executeFunc));
    }

}  // namespace qifeng
