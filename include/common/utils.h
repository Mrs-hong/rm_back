/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "common/scmd_def.h"
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

    /**
     * @brief 检查实际版本是否满足约束条件
     * @param actualVersion 实际版本号，格式 x.x.x
     * @param constraint 版本约束，支持 >=x.x.x、>x.x.x、<=x.x.x、<x.x.x、=x.x.x、x.x.x
     * @return bool 是否满足约束
     */
    bool SatisfiesVersionConstraint(const std::string& actualVersion, const std::string& constraint);

}  // namespace qifeng::scm::utils

// 用户权限相关函数
namespace qifeng::scm::utils {
    /**
     * @brief 检查用户是否为 root 用户
     * @return bool 是否为 root 用户
     */
    bool IsRootUser();

    /**
     * @brief 检查用户是否存在
     * @param user 用户名
     * @return bool 是否存在
     */
    bool ExistUser(const std::string &user);

    /**
     * @brief 获取当前用户
     * @return std::string 当前用户
     */
    std::string GetCurrentUserName();

    /**
     * @brief 设置文件（目录或者文件）权限，完全归属指定用户和 root 组
     * @param path 文件或目录路径
     * @param user 目标用户名
     * @param mode 文件权限模式（如 0750），默认为 DefaultMode
     * @return ResultMsg 操作结果
     */
    ResultMsg SetFilePermission(const std::string &path, const std::string &user, int mode = DefaultMode);

}  // namespace qifeng::scm::utils
