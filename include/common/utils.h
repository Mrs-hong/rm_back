/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "common/scmd_types.h"
#include <map>
namespace qifeng::scm::utils {
    /**
     * @brief 创建目录
     * @param dir 目录路径
     * @return ResultMsg 创建结果
     */
    ResultMsg CreateDirectory(const std::string &dir);

    /**
     * @brief 删除目录
     * @param dir 目录路径
     * @return ResultMsg 删除结果
     */
    ResultMsg ForceDeleteDirectory(const std::string &dir);

    /**
     * @description: 创建符号软连接
     * @param {string} &target 目标路径
     * @param {string} &linkPath 链接路径
     * @param {string} &linkPath 链接路径
     * @return {ResultMsg} 创建结果
     */
    ResultMsg CreateSymbolicLink(const std::string &target, const std::string &linkPath);

    /**
     * @description: 删除符号软连接
     * @param {string} &linkPath 链接路径
     * @return {ResultMsg} 删除结果
     */
    ResultMsg DeleteSymbolicLink(const std::string &linkPath);

    /**
     * @description: 解压tar包到指定目录
     * @param {string} &tarPath tar包路径
     * @param {string} &extractDir 解压目录路径
     * @return {ResultMsg} 解压结果
     */
    ResultMsg ExtractTar(const std::string &tarPath, const std::string &extractDir);

    /**
     * @description: 压缩目录为tar包
     * @param {string} &dir 目录路径
     * @param {string} &tarPath tar包路径
     * @return {ResultMsg} 压缩结果
     */
    ResultMsg CompressDirToTar(const std::string &dir, const std::string &tarPath);

    /**
     * @description: 验证tar包是否和sha256文件匹配
     * @param {string} &tarPath tar包路径
     * @param {string} &sha256Path sha256文件路径
     * @return {ResultMsg} 验证结果
     */
    ResultMsg VerifyTarWithSha256(const std::string &tarPath, const std::string &sha256Path);

    /**
     * @description: 移动目录
     * @param {string} &src 源目录路径
     * @param {string} &dst 目标目录路径
     * @return {ResultMsg} 移动结果
     */
    ResultMsg MoveDirectory(const std::string &src, const std::string &dst);

    /**
     * @brief 递归复制目录
     * @param src 源目录路径
     * @param dst 目标目录路径
     * @return ResultMsg 复制结果
     */
    ResultMsg CopyDirectory(const std::string &src, const std::string &dst);

    /**
     * @brief 清空目录内容但保留目录本身
     * @param dir 目录路径
     * @return ResultMsg 清空结果
     */
    ResultMsg ClearDirectoryContents(const std::string &dir);

    /**
     * @brief 获取路径的绝对路径
     * @param path 输入路径
     * @return std::string 绝对路径，失败时返回原路径
     */
    std::string GetAbsolutePath(const std::string &path);

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
