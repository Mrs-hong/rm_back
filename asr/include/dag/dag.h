/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMWORK_DAG_DAG_H
#define QIFENG_FRAMWORK_DAG_DAG_H

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "dag/node.h"

namespace qifeng {

    // DAG 类
    class DAG {
    public:
        DAG();
        ~DAG() = default;

        // 禁止拷贝
        DAG(const DAG&) = delete;
        DAG& operator=(const DAG&) = delete;
        DAG(DAG&&) noexcept = default;
        DAG& operator=(DAG&&) noexcept = default;

        // 添加节点（成功返回 true，重复 nodeId 返回 false）
        bool AddNode(std::shared_ptr<DAGNode> node);

        // 添加依赖关系（from 依赖 to）
        bool AddDependency(const std::string& fromNodeId, const std::string& toNodeId);

        // 添加多个依赖关系（from 依赖 toNodeIds 中的所有节点）
        bool AddDependency(const std::string& fromNodeId, const std::vector<std::string>& toNodeIds);

        // 获取节点
        std::shared_ptr<DAGNode> GetNode(const std::string& nodeId);

        // 获取所有节点
        const std::unordered_map<std::string, std::shared_ptr<DAGNode>>& GetAllNodes() const;

        // 验证 DAG（检测环）
        bool Validate() const;

        // 拓扑排序
        std::vector<std::shared_ptr<DAGNode>> TopologicalSort() const;

        // 重置 DAG
        void Reset();

    private:
        // 深度优先搜索用于检测环和拓扑排序
        bool DFS(const std::string& nodeId, std::unordered_set<std::string>& visited,
                 std::unordered_set<std::string>& recStack, std::vector<std::shared_ptr<DAGNode>>& result) const;

        // 访问节点的后继并递归 DFS
        bool VisitSuccessors(const std::shared_ptr<DAGNode>& node, std::unordered_set<std::string>& visited,
                             std::unordered_set<std::string>& recStack,
                             std::vector<std::shared_ptr<DAGNode>>& result) const;

        std::unordered_map<std::string, std::shared_ptr<DAGNode>> mNodes;
    };

}  // namespace qifeng

#endif  // QIFENG_FRAMWORK_DAG_DAG_H
