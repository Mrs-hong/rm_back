//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <future>
#include <malloc.h>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/scope_exit.h"
#include "workflow/WFTaskFactory.h"
#include "workflow/Workflow.h"

#include "common/common.h"
#include "common/config/cleanup_config.h"
#include "common/config/tmp_path_config.h"
#include "common/config/voiceprint_config.h"
#include "common/timer_manager.h"
#include "core/config/config_service.h"
#include "core/system/tasks/cleanup_task.h"

namespace qifeng_ca {

    namespace {

        // 获取文件最后修改时间距今的秒数
        int64_t GetFileAgeSeconds(const std::filesystem::path &path) {
            std::error_code ec;
            auto writeTime = std::filesystem::last_write_time(path, ec);
            if (ec) {
                return 0;
            }
            auto now = std::filesystem::file_time_type::clock::now();
            auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - writeTime);
            return diff.count();
        }

        // 毫秒时间戳转字符串(用于日志)
        std::string FormatMsTime(int64_t ms) {
            std::time_t sec = ms / 1000;
            std::tm tmBuf {};
            localtime_r(&sec, &tmBuf);
            std::array<char, 32> buf {};
            std::strftime(buf.data(), buf.size(), "%Y-%m-%d %H:%M:%S", &tmBuf);
            return std::string(buf.data());
        }

    }  // namespace

    CleanupTask &CleanupTask::GetInstance() {
        static CleanupTask Instance;
        return Instance;
    }

    void CleanupTask::Start() {
        if (mIsRunning.load(std::memory_order_acquire)) {
            SLOG_WARN << "CleanupTask already running";
            return;
        }
        mIsRunning.store(true, std::memory_order_release);

        auto &cfg = CleanupConfig::GetInstance();
        SLOG_INFO << "CleanupTask started, interval=" << cfg.GetIntervalSec() << "s, timeout=" << cfg.GetTimeoutSec()
                  << "s";

        // 首次立即执行, 之后周期调度
        auto* firstTask =
            WFTaskFactory::create_go_task(WorkflowTakeName::CleanupTask.data(), [this]() { this->DoCleanup(); });

        auto* series = Workflow::create_series_work(firstTask, [this](const SeriesWork*) { this->ScheduleNext(); });
        series->start();
    }

    void CleanupTask::Stop() {
        mIsRunning.store(false, std::memory_order_release);
        SLOG_INFO << "CleanupTask stopped";
    }

    bool CleanupTask::IsRunning() const {
        return mIsRunning.load(std::memory_order_acquire);
    }

    void CleanupTask::RegisterCleanupPath(const std::string &path) {
        if (path.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(mPathsMutex);
        if (std::find(mCleanupPaths.begin(), mCleanupPaths.end(), path) == mCleanupPaths.end()) {
            mCleanupPaths.push_back(path);
        }
    }

    void CleanupTask::ClearCleanupPaths() {
        std::lock_guard<std::mutex> lock(mPathsMutex);
        mCleanupPaths.clear();
    }

    void CleanupTask::ScheduleNext() {
        if (!mIsRunning.load(std::memory_order_acquire)) {
            return;
        }

        int intervalSec = CleanupConfig::GetInstance().GetIntervalSec();
        auto* timerTask = WFTaskFactory::create_timer_task(
            std::string(TimerName::CleanupTaskTimer), static_cast<time_t>(intervalSec), 0, [this](WFTimerTask* task) {
                if (task->get_state() != WFT_STATE_SUCCESS) {
                    SLOG_ERROR << "CleanupTaskTimer: timer callback, state=" << task->get_state()
                               << ", error=" << task->get_error();
                    return;
                }
                if (!mIsRunning.load(std::memory_order_acquire)) {
                    return;
                }
                auto* cleanupTask = WFTaskFactory::create_go_task(WorkflowTakeName::CleanupTask.data(),
                                                                  [this]() { this->DoCleanup(); });
                auto* series =
                    Workflow::create_series_work(cleanupTask, [this](const SeriesWork*) { this->ScheduleNext(); });
                series->start();
            });
        timerTask->start();
    }

    void CleanupTask::DoCleanup() {
        CleanupResult result = ExecuteWithRetry();
        SLOG_INFO << "CleanupTask::DoCleanup finished, success=" << result.mSuccess << ", timeout=" << result.mTimeout
                  << ", files=" << result.mDeletedFiles << ", audit=" << result.mDeletedAuditCount
                  << ", device=" << result.mDeletedDeviceCount;
    }

    void CleanupTask::TriggerCleanup() {
        if (mIsCleaning.load(std::memory_order_acquire)) {
            SLOG_WARN << "CleanupTask::TriggerCleanup skipped, already cleaning";
            return;
        }
        SLOG_INFO << "CleanupTask::TriggerCleanup triggered by disk low";

        auto* task =
            WFTaskFactory::create_go_task(WorkflowTakeName::CleanupTask.data(), [this]() { this->DoCleanup(); });
        task->start();
    }

    bool CleanupTask::WaitForCleanupFinish(int timeoutSec) {
        auto start = std::chrono::steady_clock::now();
        auto timeout = std::chrono::seconds(timeoutSec);
        while (mIsCleaning.load(std::memory_order_acquire)) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed >= timeout) {
                SLOG_WARN << "CleanupTask::WaitForCleanupFinish timeout, timeout=" << timeoutSec << "s";
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return true;
    }

    CleanupResult CleanupTask::ExecuteOnce() {
        return ExecuteWithRetry();
    }

    CleanupResult CleanupTask::ExecuteFileCleanupOnly() {
        CleanupResult result {};
        std::unique_lock<std::mutex> lock(mCleanupMutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            result.mSuccess = false;
            result.mMessage = "another cleanup task is running";
            return result;
        }

        mIsCleaning.store(true, std::memory_order_release);
        ScopeExit onExit([this]() { mIsCleaning.store(false, std::memory_order_release); });

        result.mDeletedFiles = CleanupTempFiles();
        result.mSuccess = (result.mDeletedFiles >= 0);
        if (!result.mSuccess) {
            result.mMessage = "temp file cleanup failed";
        }
        return result;
    }

    CleanupResult CleanupTask::ExecuteWithRetry() {
        auto &cfg = CleanupConfig::GetInstance();
        int retryCount = cfg.GetRetryCount();
        int retryIntervalSec = cfg.GetRetryIntervalSec();

        CleanupResult lastResult;
        for (int i = 0; i <= retryCount; ++i) {
            lastResult = ExecuteCleanupInternal();
            if (lastResult.mSuccess) {
                return lastResult;
            }
            SLOG_WARN << "CleanupTask::ExecuteWithRetry failed, retry=" << i << "/" << retryCount
                      << ", message=" << lastResult.mMessage;
            if (i < retryCount) {
                std::this_thread::sleep_for(std::chrono::seconds(retryIntervalSec));
            }
        }

        lastResult.mMessage = "清理任务经过" + std::to_string(retryCount) + "次重试后仍然失败: " + lastResult.mMessage;
        // SystemMessageNotifier::GetInstance().SendDiskError();
        return lastResult;
    }

    CleanupResult CleanupTask::ExecuteCleanupInternal() {
        CleanupResult result {};

        // 并发控制: 保证同一时刻只有一个清理任务在执行
        std::unique_lock<std::mutex> lock(mCleanupMutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            result.mSuccess = false;
            result.mMessage = "another cleanup task is running";
            return result;
        }

        mIsCleaning.store(true, std::memory_order_release);
        ScopeExit onExit([this]() { mIsCleaning.store(false, std::memory_order_release); });

        int timeoutSec = CleanupConfig::GetInstance().GetTimeoutSec();
        return RunCleanupWithTimeout(timeoutSec);
    }

    CleanupResult CleanupTask::RunCleanupWithTimeout(int timeoutSec) {
        std::future<CleanupResult> future = std::async(std::launch::async, [this]() {
            CleanupResult r {};
            r.mSuccess = true;

            r.mDeletedFiles = this->CleanupTempFiles();
            if (r.mDeletedFiles < 0) {
                r.mSuccess = false;
                r.mMessage = "temp file cleanup failed";
            }

            auto dbResult = this->CleanupDatabaseLogs();
            r.mDeletedAuditCount = dbResult.mDeletedAuditCount;
            r.mDeletedDeviceCount = dbResult.mDeletedDeviceCount;
            if (!dbResult.mSuccess) {
                r.mSuccess = false;
                r.mMessage = r.mMessage.empty() ? "database cleanup failed" : r.mMessage + "; database cleanup failed";
            }
            // 定时清理后, 手动触发内存回收
            malloc_trim(0);

            return r;
        });

        auto status = future.wait_for(std::chrono::seconds(timeoutSec));
        bool timeout = (status == std::future_status::timeout);
        if (timeout) {
            SLOG_WARN << "CleanupTask::RunCleanupWithTimeout timeout, continue waiting, timeout=" << timeoutSec << "s";
        }

        CleanupResult result = future.get();
        if (timeout) {
            result.mTimeout = true;
            result.mMessage = "cleanup timeout, but all methods completed: " + result.mMessage;
        }
        return result;
    }

    namespace {

        void CollectDefaultCleanupPaths(std::vector<std::string> &paths) {
            paths.push_back(VoiceprintConfig::GetInstance().GetVoiceprintTmpPath());
            paths.push_back(TmpPathConfig::GetInstance().GetDrogonTmpPath() + "/" +
                            TmpPathConfig::GetInstance().GetAudioTmpPath());
            paths.push_back(TmpPathConfig::GetInstance().GetDrogonTmpPath() + "/" +
                            TmpPathConfig::GetInstance().GetAudioUploadTmpPath());
            paths.push_back(TmpPathConfig::GetInstance().GetDrogonTmpPath() + "/" +
                            TmpPathConfig::GetInstance().GetHotwordUploadTmpPath());
            paths.push_back(TmpPathConfig::GetInstance().GetDrogonTmpPath() + "/" +
                            TmpPathConfig::GetInstance().GetHotwordTmpPath());
            paths.push_back(TmpPathConfig::GetInstance().GetDrogonTmpPath() + "/" +
                            TmpPathConfig::GetInstance().GetDocxTmpPath());
            paths.push_back(TmpPathConfig::GetInstance().GetDrogonTmpPath() + "/" +
                            TmpPathConfig::GetInstance().GetDiagonseTmpPath());
        }

        // 每目录至少保留的最近文件数。
        constexpr size_t MaxExpiredFilesPerDir = 0;

        void CollectExpiredFiles(const std::string &path, int64_t maxAgeSec,
                                 std::vector<std::pair<std::filesystem::path, int64_t>> &outFiles) {
            std::error_code ec;
            if (!std::filesystem::exists(path, ec) || !std::filesystem::is_directory(path, ec)) {
                SLOG_WARN << "CleanupTask::CollectExpiredFiles, path not a directory, path=" << path;
                return;
            }

            size_t totalFiles = 0;
            for (const auto &entry : std::filesystem::recursive_directory_iterator(path, ec)) {
                if (!entry.is_regular_file(ec)) {
                    SLOG_WARN << "CleanupTask::CollectExpiredFiles, skip non-regular file, path=" << entry.path();
                    continue;
                }
                ++totalFiles;
                int64_t ageSec = GetFileAgeSeconds(entry.path());
                if (ageSec > maxAgeSec) {
                    outFiles.emplace_back(entry.path(), ageSec);
                    SLOG_INFO << "CleanupTask: found expired file, path=" << entry.path() << ", ageSec=" << ageSec
                              << ", maxAgeSec=" << maxAgeSec;
                }
            }
            SLOG_INFO << "CleanupTask::CollectExpiredFiles, path=" << path << ", totalFiles=" << totalFiles
                      << ", expiredFiles=" << outFiles.size();
        }

        void SortFilesByAgeAsc(std::vector<std::pair<std::filesystem::path, int64_t>> &files) {
            std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) { return a.second < b.second; });
        }

        void RemoveFilesBeyondLimit(const std::vector<std::pair<std::filesystem::path, int64_t>> &files,
                                    size_t keepLimit, int64_t &deletedCount, int64_t &failedCount) {
            // 通用删除逻辑: 删除最老的(files.size() - keepLimit)个过期文件。
            // keepLimit=0 时即删除全部过期文件(保留功能禁用)。
            if (files.size() <= keepLimit) {
                SLOG_INFO << "CleanupTask::RemoveFilesBeyondLimit, skip deletion, expiredFiles=" << files.size()
                          << ", keepLimit=" << keepLimit << " (expired files count <= keepLimit, all retained)";
                return;
            }

            size_t toDelete = files.size() - keepLimit;
            SLOG_INFO << "CleanupTask::RemoveFilesBeyondLimit, start deleting " << toDelete
                      << " oldest expired files, totalExpired=" << files.size() << ", keepLimit=" << keepLimit;
            for (size_t i = keepLimit; i < files.size(); ++i) {
                std::error_code removeEc;
                std::filesystem::remove(files[i].first, removeEc);
                if (removeEc) {
                    SLOG_WARN << "CleanupTask: remove file failed, path=" << files[i].first
                              << ", ageSec=" << files[i].second << ", error=" << removeEc.message();
                    ++failedCount;
                } else {
                    SLOG_INFO << "CleanupTask: removed expired file, path=" << files[i].first
                              << ", ageSec=" << files[i].second;
                    ++deletedCount;
                }
            }
        }

        void CleanupSingleDirectory(const std::string &path, int64_t maxAgeSec, int64_t &deletedCount,
                                    int64_t &failedCount) {
            SLOG_INFO << "CleanupTask: start cleaning directory, path=" << path << ", maxAgeSec=" << maxAgeSec;
            std::vector<std::pair<std::filesystem::path, int64_t>> expiredFiles;
            CollectExpiredFiles(path, maxAgeSec, expiredFiles);
            SortFilesByAgeAsc(expiredFiles);
            RemoveFilesBeyondLimit(expiredFiles, MaxExpiredFilesPerDir, deletedCount, failedCount);
            SLOG_INFO << "CleanupTask: finished cleaning directory, path=" << path << ", deleted=" << deletedCount
                      << ", failed=" << failedCount;
        }

    }  // namespace

    int64_t CleanupTask::CleanupTempFiles() {
        int64_t deletedCount = 0;
        int64_t failedCount = 0;
        auto maxAgeSec = CleanupConfig::GetInstance().GetTempFileMaxAgeSec();

        std::vector<std::string> paths;
        {
            std::lock_guard<std::mutex> lock(mPathsMutex);
            paths = mCleanupPaths;
            if (paths.empty()) {
                CollectDefaultCleanupPaths(paths);
            }
        }

        for (const auto &path : paths) {
            CleanupSingleDirectory(path, maxAgeSec, deletedCount, failedCount);
        }

        SLOG_INFO << "CleanupTask::CleanupTempFiles finished, deleted=" << deletedCount << ", failed=" << failedCount;
        return (failedCount > 0 && deletedCount == 0) ? -1 : deletedCount;
    }

    CleanupDbResult CleanupTask::CleanupDatabaseLogs() {
        ConfigService configSvc;
        GetBaseConfigResponse cfg;
        configSvc.GetWebBaseConfig(&cfg);

        int32_t logCleanDays = cfg.log_clean_days();
        if (logCleanDays <= 0) {
            logCleanDays = 90;
        }

        int64_t nowMs = static_cast<int64_t>(GetTimeMs());
        int64_t beforeTime = nowMs - static_cast<int64_t>(logCleanDays) * 24LL * 60 * 60 * 1000;

        SLOG_INFO << "CleanupTask::CleanupDatabaseLogs, logCleanDays=" << logCleanDays
                  << ", beforeTime=" << FormatMsTime(beforeTime);

        CleanupDao dao;
        return dao.DeleteExpiredRecords(beforeTime, beforeTime);
    }

}  // namespace qifeng_ca
