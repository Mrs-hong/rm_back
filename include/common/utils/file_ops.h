/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "common/scmd_def.h"
#include "common/types.h"

#include <string>
#include <vector>

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
     * @return {ResultMsg} 创建结果
     */
    ResultMsg CreateSymbolicLink(const std::string &target, const std::string &linkPath);

    /**
     * @description: 删除符号软连接
     * @param {string} &linkPath 链接路径
     * @return {ResultMsg} 创建结果
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
     * @brief 获取目录下所有满足后缀的文件的绝对路径
     * @param outResul 输出文件绝对路径列表
     * @param dir 目录路径
     * @param suffix 文件名后缀，默认空字符串表示获取所有文件
     * @return ResultMsg 操作结果，失败时包含错误信息
     */
    ResultMsg GetAllFilesInDir(std::vector<std::string> &outResul, const std::string &dir,
                               const std::string &suffix = "");

}  // namespace qifeng::scm::utils
