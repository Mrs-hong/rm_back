//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_SYSTEM_TASKS_CLEANUP_TASK_H
#define QIFENG_CA_INCLUDE_CORE_SYSTEM_TASKS_CLEANUP_TASK_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "dao/cleanup_dao.h"

namespace qifeng_ca {

    // 单次清理结果
    struct CleanupResult {
        bool mSuccess = false;
        bool mTimeout = false;
        int64_t mDeletedFiles = 0;
        int64_t mDeletedAuditCount = 0;
        int64_t mDeletedDeviceCount = 0;
        std::string mMessage;
    };

    // 数据清理定时任务:
    // - 每1小时定时执行一次数据清理
    // - 支持磁盘空间不足时被动触发
    // - 具备并发控制、超时控制、失败重试、日志与告警
    class CleanupTask {
    public:
        static CleanupTask &GetInstance();

        void Start();
        void Stop();
        bool IsRunning() const;

        // 手动触发一次清理(磁盘空间不足时调用), 不阻塞调用方
        void TriggerCleanup();

        // 等待当前清理任务完成, 最多等待 timeoutSec 秒
        bool WaitForCleanupFinish(int timeoutSec);

        // 同步执行一次完整清理(含超时与重试)
        CleanupResult ExecuteOnce();

        // 同步执行一次仅清理临时文件(供单元测试使用, 不访问数据库)
        CleanupResult ExecuteFileCleanupOnly();

        // 注册清理目录(默认已包含业务临时目录, 支持扩展)
        void RegisterCleanupPath(const std::string &path);

        // 清空已注册的清理目录(供单元测试使用)
        void ClearCleanupPaths();

    private:
        CleanupTask() = default;
        ~CleanupTask() = default;
        CleanupTask(const CleanupTask &) = delete;
        CleanupTask &operator=(const CleanupTask &) = delete;
        CleanupTask(CleanupTask &&) = delete;
        CleanupTask &operator=(CleanupTask &&) = delete;

        void ScheduleNext();
        void DoCleanup();

        int64_t CleanupTempFiles();
        CleanupDbResult CleanupDatabaseLogs();

        CleanupResult ExecuteWithRetry();
        CleanupResult ExecuteCleanupInternal();
        CleanupResult RunCleanupWithTimeout(int timeoutSec);

    private:
        std::atomic<bool> mIsRunning {false};
        std::atomic<bool> mIsCleaning {false};
        std::mutex mCleanupMutex;
        std::mutex mPathsMutex;
        std::vector<std::string> mCleanupPaths;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_SYSTEM_TASKS_CLEANUP_TASK_H
