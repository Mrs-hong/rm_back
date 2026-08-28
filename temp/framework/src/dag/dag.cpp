/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "dag/dag.h"
#include <algorithm>

namespace qifeng {

    // DAG 实现
    DAG::DAG() = default;

    bool DAG::AddNode(std::shared_ptr<DAGNode> node) {
        const auto& nodeId = node->GetId();
        if (mNodes.find(nodeId) != mNodes.end()) {
            return false;  // 节点已存在
        }
        mNodes[nodeId] = std::move(node);
        return true;
    }

    bool DAG::AddDependency(const std::string& fromNodeId, const std::string& toNodeId) {
        auto fromIt = mNodes.find(fromNodeId);
        auto toIt = mNodes.find(toNodeId);

        if (fromIt == mNodes.end() || toIt == mNodes.end()) {
            return false;
        }

        fromIt->second->AddDependency(toIt->second);
        toIt->second->AddSuccessor(fromIt->second);
        return true;
    }

    bool DAG::AddDependency(const std::string& fromNodeId, const std::vector<std::string>& toNodeIds) {
        bool allSuccess = true;
        for (const auto& toNodeId : toNodeIds) {
            if (!AddDependency(fromNodeId, toNodeId)) {
                allSuccess = false;
            }
        }
        return allSuccess;
    }

    std::shared_ptr<DAGNode> DAG::GetNode(const std::string& nodeId) {
        auto it = mNodes.find(nodeId);
        return (it != mNodes.end()) ? it->second : nullptr;
    }

    const std::unordered_map<std::string, std::shared_ptr<DAGNode>>& DAG::GetAllNodes() const {
        return mNodes;
    }

    bool DAG::Validate() const {
        std::unordered_set<std::string> visited;
        std::unordered_set<std::string> recStack;
        std::vector<std::shared_ptr<DAGNode>> dummy;

        for (const auto& pair : mNodes) {
            if (!visited.count(pair.first)) {
                if (DFS(pair.first, visited, recStack, dummy)) {
                    return false;  // 存在环
                }
            }
        }
        return true;
    }

    std::vector<std::shared_ptr<DAGNode>> DAG::TopologicalSort() const {
        std::unordered_set<std::string> visited;
        std::unordered_set<std::string> recStack;
        std::vector<std::shared_ptr<DAGNode>> result;

        for (const auto& pair : mNodes) {
            if (!visited.count(pair.first)) {
                DFS(pair.first, visited, recStack, result);
            }
        }

        std::reverse(result.begin(), result.end());
        return result;
    }

    void DAG::Reset() {
        for (auto& pair : mNodes) {
            pair.second->Reset();
        }
    }

    bool DAG::DFS(const std::string& nodeId, std::unordered_set<std::string>& visited,
                  std::unordered_set<std::string>& recStack, std::vector<std::shared_ptr<DAGNode>>& result) const {
        visited.insert(nodeId);
        recStack.insert(nodeId);

        auto nodeIt = mNodes.find(nodeId);
        if (nodeIt == mNodes.end()) {
            recStack.erase(nodeId);
            return false;
        }

        auto node = nodeIt->second;
        bool hasCycle = VisitSuccessors(node, visited, recStack, result);

        recStack.erase(nodeId);
        if (!hasCycle) {
            result.push_back(node);
        }
        return hasCycle;
    }

    bool DAG::VisitSuccessors(const std::shared_ptr<DAGNode>& node, std::unordered_set<std::string>& visited,
                              std::unordered_set<std::string>& recStack,
                              std::vector<std::shared_ptr<DAGNode>>& result) const {
        for (const auto& successor : node->GetSuccessors()) {
            if (!visited.count(successor->GetId())) {
                if (DFS(successor->GetId(), visited, recStack, result)) {
                    return true;
                }
            } else if (recStack.count(successor->GetId())) {
                return true;  // 存在环
            }
        }
        return false;
    }

}  // namespace qifeng
