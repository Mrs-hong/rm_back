/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/utils/dependency.h"

#include "common/utils/version.h"

#include <algorithm>
#include <map>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
    using qifeng::scm::CheckDependencyError;
    using qifeng::scm::ServiceDefinition;
    enum class VisitState { UNVISITED, IN_PROGRESS, COMPLETED };

    // 检查从当前节点是否存在循环依赖
    bool CheckCircularFromNode(const std::string &serviceName, const std::map<std::string, ServiceDefinition> &services,
                               std::map<std::string, VisitState> &state, std::vector<std::string> &currentPath) {
        state[serviceName] = VisitState::IN_PROGRESS;
        currentPath.push_back(serviceName);

        auto it = services.find(serviceName);
        if (it != services.end()) {
            for (const auto &[depName, depVersion] : it->second.dependencies) {
                if (state[depName] == VisitState::IN_PROGRESS) {
                    return true;
                }
                if (CheckCircularFromNode(depName, services, state, currentPath)) {
                    return true;
                }
            }
        }

        currentPath.pop_back();
        state[serviceName] = VisitState::COMPLETED;
        return false;
    }

    // 标记循环依赖路径中的所有节点
    void MarkCircularInPath(const std::string &circularNode, const std::vector<std::string> &currentPath,
                            std::vector<CheckDependencyError> &errors) {
        bool found = false;
        for (const auto &node : currentPath) {
            if (node == circularNode) {
                found = true;
            }
            if (found) {
                errors.push_back({CheckDependencyError::Status::CIRCULAR, node});
            }
        }
    }

    // 检测所有服务的循环依赖
    void DetectCircularDependencies(const std::map<std::string, ServiceDefinition> &services,
                                    std::vector<CheckDependencyError> &errors) {
        std::map<std::string, VisitState> state;

        for (const auto &[serviceName, serviceDef] : services) {
            if (state[serviceName] == VisitState::UNVISITED) {
                std::vector<std::string> currentPath;
                if (CheckCircularFromNode(serviceName, services, state, currentPath)) {
                    MarkCircularInPath(serviceName, currentPath, errors);
                }
            }
        }
    }

    // 检测缺失服务和版本冲突
    void DetectMissingAndVersionConflicts(const std::map<std::string, ServiceDefinition> &services,
                                          std::vector<CheckDependencyError> &errors) {
        for (const auto &[serviceName, serviceDef] : services) {
            for (const auto &[depName, depVersion] : serviceDef.dependencies) {
                auto depIt = services.find(depName);
                if (depIt == services.end()) {
                    errors.push_back({CheckDependencyError::Status::MISSING, serviceName});
                } else if (!qifeng::scm::utils::SatisfiesVersionConstraint(depIt->second.version, depVersion)) {
                    errors.push_back({CheckDependencyError::Status::VERSION_CONFLICT, serviceName});
                }
            }
        }
    }
}  // namespace

namespace qifeng::scm::utils {
    std::vector<CheckDependencyError> CheckDependenciesMap(const std::map<std::string, ServiceDefinition> &services) {
        std::vector<CheckDependencyError> errors;

        DetectCircularDependencies(services, errors);
        DetectMissingAndVersionConflicts(services, errors);

        return errors;
    }

    // 构建依赖图的邻接表和入度表
    void BuildDependencyGraph(const std::map<std::string, ServiceDefinition> &services,
                              std::unordered_map<std::string, std::vector<std::string>> &adj,
                              std::unordered_map<std::string, int> &inDegree) {
        for (const auto &[name, def] : services) {
            inDegree[name] = 0;
            adj[name] = {};
        }

        for (const auto &[name, def] : services) {
            for (const auto &[depName, depVersion] : def.dependencies) {
                if (services.count(depName)) {
                    adj[depName].push_back(name);
                    inDegree[name]++;
                }
            }
        }
    }

    // 执行 Kahn BFS 拓扑排序，返回排序结果
    std::vector<std::string> DoTopologicalSort(std::unordered_map<std::string, std::vector<std::string>> &adj,
                                               std::unordered_map<std::string, int> inDegree) {
        std::vector<std::string> order;
        std::queue<std::string> q;

        for (const auto &[name, degree] : inDegree) {
            if (degree == 0) {
                q.push(name);
            }
        }

        while (!q.empty()) {
            std::string node = q.front();
            q.pop();
            order.push_back(node);

            auto it = adj.find(node);
            if (it == adj.end()) {
                continue;
            }
            for (const auto &neighbor : it->second) {
                inDegree[neighbor]--;
                if (inDegree[neighbor] == 0) {
                    q.push(neighbor);
                }
            }
        }

        return order;
    }

    ServiceSequence ComputeServiceSequence(const std::map<std::string, ServiceDefinition> &services) {
        ServiceSequence result;

        if (services.empty()) {
            return result;
        }

        // 先用 CheckDependenciesMap 检查循环依赖，避免重复构建依赖图
        auto errors = CheckDependenciesMap(services);
        for (const auto &err : errors) {
            if (err.status == CheckDependencyError::Status::CIRCULAR) {
                return result;
            }
        }

        std::unordered_map<std::string, std::vector<std::string>> adj;
        std::unordered_map<std::string, int> inDegree;
        BuildDependencyGraph(services, adj, inDegree);

        result.startOrder = DoTopologicalSort(adj, inDegree);

        // 缓存反向邻接表（谁依赖了谁），供 GetDependentServices 使用
        for (const auto &[depName, dependents] : adj) {
            result.reverseAdj[depName] = dependents;
        }

        result.stopOrder = result.startOrder;
        std::reverse(result.stopOrder.begin(), result.stopOrder.end());

        return result;
    }
}  // namespace qifeng::scm::utils
