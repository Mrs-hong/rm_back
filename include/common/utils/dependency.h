/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "common/scmd_types.h"
#include "common/types.h"

#include <map>
#include <vector>

namespace qifeng::scm::utils {
    /**
     * @brief 检查服务依赖关系是否版本冲突、缺失的服务、循环依赖影响的所有服务
     * @param services 所有服务的 map
     * @return std::vector<CheckDependencyError> 检查结果列表
     */
    std::vector<CheckDependencyError> CheckDependenciesMap(const std::map<std::string, ServiceDefinition> &services);

    /**
     * @brief 根据服务依赖关系计算启动/停止顺序（拓扑排序）
     * @param services 所有服务的 map
     * @return ServiceSequence 启动/停止序列，若存在循环依赖则返回空序列
     */
    ServiceSequence ComputeServiceSequence(const std::map<std::string, ServiceDefinition> &services);

}  // namespace qifeng::scm::utils
