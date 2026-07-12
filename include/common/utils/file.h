/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "common/types.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <unistd.h>
#include <vector>

namespace qifeng::scm::utils {
    /**
     * @brief 创建目录
     * @param dir 目录路径
     * @return ResultMsg 创建结果
     */
    ResultMsg CreateDirectory(const std::string &dir);

    /**
     * @brief 判断两个路径是否指向同一位置
     * @param path1 路径1
     * @param path2 路径2
     * @return ResultMsg 比较结果
     */
    ResultMsg IsSamePath(const std::string &path1, const std::string &path2);

    /**
     * @brief 删除目录
     * @param dir 目录路径
     * @return ResultMsg 删除结果
     */
    ResultMsg ForceDeleteDirectory(const std::string &dir);

    /**
     * @brief 创建符号软连接
     * @param target 目标路径
     * @param linkPath 链接路径
     * @return ResultMsg 创建结果
     */
    ResultMsg CreateSymbolicLink(const std::string &target, const std::string &linkPath);

    /**
     * @brief 删除符号软连接
     * @param linkPath 链接路径
     * @return ResultMsg 创建结果
     */
    ResultMsg DeleteSymbolicLink(const std::string &linkPath);

    /**
     * @brief 解压 tar 包到指定目录
     * @param tarPath tar 包路径
     * @param extractDir 解压目录路径
     * @return ResultMsg 解压结果
     */
    ResultMsg ExtractTar(const std::string &tarPath, const std::string &extractDir);

    /**
     * @brief 压缩目录为 tar 包
     * @param dir 目录路径
     * @param tarPath tar 包路径
     * @return ResultMsg 压缩结果
     */
    ResultMsg CompressDirToTar(const std::string &dir, const std::string &tarPath);

    /**
     * @brief 验证 tar 包是否和 sha256 文件匹配
     * @param tarPath tar 包路径
     * @param sha256Path sha256 文件路径
     * @return ResultMsg 验证结果
     */
    ResultMsg VerifyTarWithSha256(const std::string &tarPath, const std::string &sha256Path);

    /**
     * @brief 移动目录
     * @param src 源目录路径
     * @param dst 目标目录路径
     * @return ResultMsg 移动结果
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
     * @brief 删除文件
     * @param filePath 文件路径
     * @return ResultMsg 删除结果
     */
    ResultMsg RemoveFile(const std::string &filePath);

    /**
     * @brief 获取目录下所有满足后缀的文件的绝对路径
     * @param outResult 输出文件绝对路径列表
     * @param dir 目录路径
     * @param suffix 文件名后缀，默认空字符串表示获取所有文件
     * @return ResultMsg 操作结果，失败时包含错误信息
     */
    ResultMsg GetAllFilesInDir(std::vector<std::string> &outResult, const std::string &dir,
                               const std::string &suffix = "");

    /**
     * @brief 生成唯一临时目录名
     * @details 结合进程 PID 与 steady_clock 计数避免并发冲突
     * @return std::string 临时目录名（不含父路径）
     */
    inline std::string GenerateTempDirName(const std::string &prefix = "scmd_install_") {
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        return prefix + std::to_string(getpid()) + "_" + std::to_string(now);
    }

    /**
     * @brief 生成唯一临时目录完整路径
     * @param tempDir 父目录，若为空或不可创建则回退到系统临时目录
     * @return std::string 临时目录完整路径
     */
    inline std::string GenerateTempDir(const std::string &tempDir = "") {
        namespace fs = std::filesystem;
        if (!tempDir.empty()) {
            auto res = CreateDirectory(tempDir);
            if (res.IsDefaultSuccess()) {
                return (fs::path(tempDir) / GenerateTempDirName()).string();
            }
        }
        return (fs::temp_directory_path() / GenerateTempDirName()).string();
    }

    /**
     * @brief 将目录下第一个子目录重命名为指定名称
     * @details 遍历 extractDir，将第一个遇到的目录条目重命名为 newName
     * @param extractDir 待处理的目录
     * @param newName 新目录名
     */
    inline void RenameFirstSubdirectory(const std::string &extractDir, const std::string &newName) {
        namespace fs = std::filesystem;
        for (auto &entry : fs::directory_iterator(extractDir)) {
            if (entry.is_directory()) {
                fs::rename(entry.path(), entry.path().parent_path() / newName);
                break;
            }
        }
    }

    /**
     * @brief 解压 tar 包并在失败时清理临时目录
     * @details 封装 ExtractTar，失败时删除已创建的解压目录避免污染
     * @param tarPath tar 包路径
     * @param extractDir 解压目标目录
     * @return ResultMsg 操作结果
     */
    inline ResultMsg ExtractTarWithCleanup(const std::string &tarPath, const std::string &extractDir) {
        auto result = ExtractTar(tarPath, extractDir);
        if (!result.IsDefaultSuccess()) {
            ForceDeleteDirectory(extractDir);
            return MakeError("Failed to extract tar file: " + result.msg);
        }
        return result;
    }
}  // namespace qifeng::scm::utils
