/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include "common/types.h"
#include "common/utils.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace qifeng::scm::utils {
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
            if (res.IsDefalutSuccess()) {
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
        if (!result.IsDefalutSuccess()) {
            ForceDeleteDirectory(extractDir);
            return MakeError("Failed to extract tar file: " + result.msg);
        }
        return result;
    }
}  // namespace qifeng::scm::utils
