/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <string>
#include <vector>

namespace qifeng::scm::utils {
    /**
     * @brief 读取指定 systemd unit 最近 N 条 journal 日志
     * @details 通过 sd-journal API 读取，等价于 journalctl -u <unit>.service -n <count>，
     *          输出格式与 journalctl -o short 一致:
     *          "MMM DD HH:MM:SS hostname identifier[pid]: message"
     *          用于 slog 命令回退读取和服务启停后同步 systemd 操作记录到服务日志文件。
     * @param unitName systemd 单元名（不含 .service 后缀，如 "scmd_xxx" 或 "qifeng-scmd"）
     * @param count 日志行数（<=0 时使用默认 10）
     * @return std::vector<std::string> 从旧到新的日志行列表；打开/迭代失败时返回空
     */
    std::vector<std::string> ReadJournalLastN(const std::string& unitName, int count);

    /**
     * @brief 将 journal 日志行列表拼接为单字符串（每行末尾含换行）
     * @param lines 日志行列表
     * @return std::string 拼接后的字符串
     */
    std::string JoinJournalLines(const std::vector<std::string>& lines);

    /**
     * @brief 读取文件最后 N 行
     * @details 用于读取服务日志文件（StandardOutput 重定向的文件）。
     *          文件不存在或不可读时返回空列表。
     * @param filePath 文件路径
     * @param count 行数（<=0 时返回所有行）
     * @return std::vector<std::string> 从旧到新的行列表
     */
    std::vector<std::string> ReadFileLastNLines(const std::string& filePath, int count);

    /**
     * @brief 追加内容到文件末尾
     * @details 用于将 systemd 操作记录（journal）追加到服务日志文件。
     *          自动创建父目录。
     * @param filePath 文件路径
     * @param content 追加内容
     * @return bool 是否成功
     */
    bool AppendToFile(const std::string& filePath, const std::string& content);

}  // namespace qifeng::scm::utils
