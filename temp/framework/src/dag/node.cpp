/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/logger.h"
#include "dag/node.h"

namespace qifeng {

    // CommonTaskData 实现
    bool CommonTaskData::Contains(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mMutex);
        return mValues.find(key) != mValues.end();
    }

    void CommonTaskData::Erase(const std::string& key) {
        std::lock_guard<std::mutex> lock(mMutex);
        mValues.erase(key);
    }

    void CommonTaskData::Clear() {
        std::lock_guard<std::mutex> lock(mMutex);
        mValues.clear();
    }

    // DAGNode 实现
    DAGNode::DAGNode(const std::string& nodeId) : mNodeId(nodeId), mState(DAGNodeState::PENDING) {
    }

    const std::string& DAGNode::GetId() const {
        return mNodeId;
    }

    DAGNodeState DAGNode::GetState() const {
        return mState;
    }

    void DAGNode::SetState(DAGNodeState state) {
        mState = state;
    }

    void DAGNode::AddDependency(std::shared_ptr<DAGNode> dependency) {
        if (!dependency) {
            return;
        }
        mDependencies.push_back(dependency);
        dependency->AddSuccessor(shared_from_this());
    }

    const std::vector<std::shared_ptr<DAGNode>>& DAGNode::GetDependencies() const {
        return mDependencies;
    }

    void DAGNode::AddSuccessor(std::shared_ptr<DAGNode> successor) {
        if (!successor) {
            return;
        }
        mSuccessors.push_back(successor);
    }

    const std::vector<std::shared_ptr<DAGNode>>& DAGNode::GetSuccessors() const {
        return mSuccessors;
    }

    const std::string& DAGNode::GetError() const {
        return mError;
    }

    void DAGNode::SetError(const std::string& error) {
        mError = error;
    }

    void DAGNode::Reset() {
        mState = DAGNodeState::PENDING;
        mError.clear();
    }

    bool DAGNode::CanExecute(const std::shared_ptr<CommonTaskData>& sharedData) const {
        // 检查是否被取消
        if (sharedData && sharedData->IsCancelled()) {
            SLOG_INFO << "Task cancelled: " << mNodeId;
            return false;
        }

        // 检查数据是否可信
        if (!sharedData->IsDataTrusted()) {
            SLOG_WARN << "Task input data not trusted: " << mNodeId;
            return false;
        }

        return true;
    }

}  // namespace qifeng
