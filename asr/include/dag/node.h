/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMWORK_DAG_NODE_H
#define QIFENG_FRAMWORK_DAG_NODE_H

#include <any>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace qifeng {

    // 前向声明
    class DAGNode;

    // DAG 节点状态
    enum class DAGNodeState {
        PENDING,    // 等待执行
        READY,      // 就绪，可执行
        RUNNING,    // 正在执行
        COMPLETED,  // 执行完成
        FAILED      // 执行失败
    };

    // DAG 共享任务数据
    class CommonTaskData {
    public:
        CommonTaskData() = default;

        template <typename T>
        void Set(const std::string& key, T value) {
            std::lock_guard<std::mutex> lock(mMutex);
            mValues[key] = std::any(std::move(value));
        }

        template <typename T>
        T* Get(const std::string& key) {
            std::lock_guard<std::mutex> lock(mMutex);
            auto it = mValues.find(key);
            if (it == mValues.end()) {
                return nullptr;
            }
            return std::any_cast<T>(&it->second);
        }

        template <typename T>
        const T* Get(const std::string& key) const {
            std::lock_guard<std::mutex> lock(mMutex);
            auto it = mValues.find(key);
            if (it == mValues.end()) {
                return nullptr;
            }
            return std::any_cast<T>(&it->second);
        }

        bool Contains(const std::string& key) const;

        void Erase(const std::string& key);

        void Clear();

        // 取消相关方法
        void Cancel() {
            mCancelled.store(true);
        }
        bool IsCancelled() const {
            return mCancelled.load();
        }

        // 数据可信度相关方法
        void SetDataTrusted(bool trusted) {
            mDataTrusted.store(trusted);
        }
        bool IsDataTrusted() const {
            return mDataTrusted.load();
        }

    private:
        mutable std::mutex mMutex;
        std::unordered_map<std::string, std::any> mValues;
        std::atomic<bool> mCancelled {false};
        std::atomic<bool> mDataTrusted {false};  // 数据是否可信标志
    };

    // DAG 节点基类
    class DAGNode : public std::enable_shared_from_this<DAGNode> {
    public:
        explicit DAGNode(const std::string& nodeId);
        virtual ~DAGNode() = default;

        // 禁止拷贝
        DAGNode(const DAGNode&) = delete;
        DAGNode& operator=(const DAGNode&) = delete;
        DAGNode(DAGNode&&) noexcept = default;
        DAGNode& operator=(DAGNode&&) noexcept = default;

        // 获取节点 ID
        const std::string& GetId() const;

        // 获取节点状态
        DAGNodeState GetState() const;

        // 设置节点状态
        void SetState(DAGNodeState state);

        // 执行节点 - 统一使用带参版本
        // 子类只需要实现这个方法
        virtual bool Execute(const std::shared_ptr<CommonTaskData>& sharedData) = 0;

        // 添加依赖节点
        void AddDependency(std::shared_ptr<DAGNode> dependency);

        // 获取依赖节点列表
        const std::vector<std::shared_ptr<DAGNode>>& GetDependencies() const;

        // 添加后继节点
        void AddSuccessor(std::shared_ptr<DAGNode> successor);

        // 获取后继节点列表
        const std::vector<std::shared_ptr<DAGNode>>& GetSuccessors() const;

        // 获取错误信息
        const std::string& GetError() const;

        // 设置错误信息
        void SetError(const std::string& error);

        // 重置节点状态
        void Reset();

        // 检查任务是否可以执行（取消检查、数据可信检查）
        bool CanExecute(const std::shared_ptr<CommonTaskData>& sharedData) const;

    protected:
        std::string mNodeId;
        DAGNodeState mState;
        std::vector<std::shared_ptr<DAGNode>> mDependencies;
        std::vector<std::shared_ptr<DAGNode>> mSuccessors;
        std::string mError;
    };

}  // namespace qifeng

#endif  // QIFENG_FRAMWORK_DAG_NODE_H
