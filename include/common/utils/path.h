/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <filesystem>
#include <string>

namespace qifeng::scm::utils {
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
     * @brief 获取目录下唯一的顶层条目名
     * @details 用于 tar 解压后确定模型名：要求目录下有且仅有一个顶层条目，
     *          返回该条目的 filename；若目录为空、有多个条目或唯一条目不是目录则返回空字符串。
     * @param dir 目录路径
     * @return std::string 唯一顶层条目名，或空字符串
     */
    std::string GetSingleTopLevelEntryName(const std::string &dir);

}  // namespace qifeng::scm::utils
