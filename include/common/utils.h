/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "common/scmd_def.h"
#include "common/scmd_types.h"
#include "common/types.h"
#include <filesystem>
#include <map>
#include <string>
namespace qifeng::scm::utils {
    /**
     * @brief 创建目录
     * @param dir 目录路径
     * @return ResultMsg 创建结果
     */
    ResultMsg CreateDirectory(const std::string &dir);

    ResultMsg IsSamePath(const std::string &path1, const std::string &path2);

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

    ResultMsg RemoveFile(const std::string &filePath);

    /**
     * @brief 获取路径的绝对路径
     * @param path 输入路径
     * @return std::string 绝对路径，失败时返回原路径
     */
    std::string GetAbsolutePath(const std::string &path);

    /**
     * @brief 安全拼接多个路径片段，自动处理斜杠冗余并规范化结果
     *
     * 使用 std::filesystem::path::operator/ 进行拼接，消除重复斜杠、
     * 尾部/首部斜杠等边界问题，并通过 lexically_normal() 消除路径中的
     * . 和 .. 组件，生成简洁规范的路径。
     *
     * @param base  基础路径
     * @param segments  后续路径片段（可变参数，支持任意数量）
     * @return std::string 拼接并规范化后的路径字符串
     *
     * @example
     *   JoinPath("/data/", "/logs/")    → "/data/logs"
     *   JoinPath("/var/lib", "qifeng", "data") → "/var/lib/qifeng/data"
     */
    template <typename... Paths>
    std::string JoinPath(const std::string &base, const Paths &... segments) {
        namespace fs = std::filesystem;
        fs::path result(base);
        ((result /= segments), ...);
        return result.lexically_normal().string();
    }

    /**
     * @brief 规范化路径，消除路径中的 . 和 .. 组件
     *
     * 使用 lexically_normal() 进行纯词法规范化，不访问文件系统，
     * 因此对不存在的路径同样有效。
     *
     * @param path 原始路径
     * @return std::string 规范化后的路径字符串
     *
     * @example
     *   NormalizePath("/data/./../logs") → "/logs"
     *   NormalizePath("/var///lib/")     → "/var/lib"
     */
    inline std::string NormalizePath(const std::string &path) {
        namespace fs = std::filesystem;
        return fs::path(path).lexically_normal().string();
    }

    /**
     * @brief 获取目录下所有满足后缀的文件的绝对路径
     * @param outResul 输出文件绝对路径列表
     * @param dir 目录路径
     * @param suffix 文件名后缀，默认空字符串表示获取所有文件
     * @return ResultMsg 操作结果，失败时包含错误信息
     */
    ResultMsg GetAllFilesInDir(std::vector<std::string> &outResul, const std::string &dir,
                               const std::string &suffix = "");

    /**
     * @brief 获取当前本地时间的格式化字符串
     * @return std::string 格式 "YYYY-MM-DD HH:MM:SS"
     */
    std::string GetCurrentTimeString();

    /**
     * @brief 写入升级结果 JSON 文件
     * @details 写入格式对齐 upgrade_result.json 模板：
     *          { "upgrade_success": <bool>, "upgrade_time": "<time>",
     *            "upgrade_version": "<version>", "defeat_reason": "<reason>" }
     *          会自动创建父目录，覆盖写。
     * @param resultPath 结果文件绝对路径
     * @param success 升级是否成功
     * @param upgradeTime 升级时间字符串（格式 YYYY-MM-DD HH:MM:SS）
     * @param upgradeVersion 升级到的目标版本号（失败场景若无法解析则为空）
     * @param defeatReason 失败原因（成功时可为空）
     * @return ResultMsg 写入结果
     */
    ResultMsg WriteUpgradeResult(const std::string &resultPath, bool success, const std::string &upgradeTime,
                                const std::string &upgradeVersion, const std::string &defeatReason);

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
     * @brief 比较两个版本号
     * @param lhs 左操作数版本号，格式 x.x.x
     * @param rhs 右操作数版本号，格式 x.x.x
     * @return int lhs < rhs 返回 -1，lhs == rhs 返回 0，lhs > rhs 返回 1
     */
    int CompareVersion(const std::string &lhs, const std::string &rhs);

    /**
     * @brief 检查实际版本是否满足约束条件
     * @param actualVersion 实际版本号，格式 x.x.x
     * @param constraint 版本约束，支持 >=x.x.x、>x.x.x、<=x.x.x、<x.x.x、=x.x.x、x.x.x
     * @return bool 是否满足约束
     */
    bool SatisfiesVersionConstraint(const std::string &actualVersion, const std::string &constraint);

    /**
     * @brief 获取目录下唯一的顶层条目名
     * @details 用于 tar 解压后确定模型名：要求目录下有且仅有一个顶层条目，
     *          返回该条目的 filename；若目录为空、有多个条目或唯一条目不是目录则返回空字符串。
     * @param dir 目录路径
     * @return std::string 唯一顶层条目名，或空字符串
     */
    std::string GetSingleTopLevelEntryName(const std::string &dir);

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

// 密码生成、加密相关
namespace qifeng::scm::utils {
    /**
     * @brief 生成随机密码
     * @param length 密码长度
     * @return std::string 随机密码
     */
    std::string GenerateRandomPassword(int length = 16);
}  // namespace qifeng::scm::utils
