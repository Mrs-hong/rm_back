/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#pragma once

#include <string>

namespace qifeng::scm {

    /**
     * @brief 关键操作记录结构体
     * @details 记录最后一次操作的完整上下文信息，用于scmd异常终止后的操作恢复
     * result语义：0成功 1失败 2进行中（被异常终止）
     */
    struct KeyOperationRecord {
        std::string optName;      // 操作名称: install/upgrade/uninstall/start/stop/initDb/initSql
        std::string serviceName;  // 服务名称
        int result {0};           // 操作结果: 0成功 1失败 2进行中
        std::string tarDir;       // install/upgrade时的软件包路径
        std::string sqlDir;       // initDb/initSql时的SQL脚本目录

        bool IsValid() const { return !optName.empty(); }
    };

    /**
     * @brief 关键操作记录器
     * @details 对last_key_opt.yaml文件进行读写操作，记录最后一次操作的执行状态和上下文
     * 用于scmd异常终止后的操作恢复
     */
    class KeyOperationRecorder {
    public:
        KeyOperationRecorder() = default;
        ~KeyOperationRecorder() = default;

        KeyOperationRecorder(const KeyOperationRecorder&) = delete;
        KeyOperationRecorder& operator=(const KeyOperationRecorder&) = delete;
        KeyOperationRecorder(KeyOperationRecorder&&) = delete;
        KeyOperationRecorder& operator=(KeyOperationRecorder&&) = delete;

        /**
         * @brief 记录关键操作（操作前写入，result=2表示进行中）
         * @param record 操作记录结构体
         */
        void RecordOperation(const KeyOperationRecord& record);

        /**
         * @brief 更新操作结果（操作完成后更新result字段）
         * @param result 操作结果: 0成功 1失败
         */
        void UpdateResult(int result);

        /**
         * @brief 读取最后一次操作记录
         * @param record 输出参数，操作记录
         * @return 是否成功读取到有效记录
         */
        bool LoadLastOperation(KeyOperationRecord& record);

        /**
         * @brief 清除操作记录（操作成功完成后或恢复完成后调用）
         */
        void Clear();

        /**
         * @brief 设置持久化文件路径
         * @param filePath last_key_opt.yaml 文件路径
         */
        void SetFilePath(const std::string& filePath);

        /**
         * @brief 获取持久化文件路径
         * @return 文件路径
         */
        const std::string& GetFilePath() const;

    private:
        std::string mFilePath;  // last_key_opt.yaml 文件路径
    };
}  // namespace qifeng::scm
