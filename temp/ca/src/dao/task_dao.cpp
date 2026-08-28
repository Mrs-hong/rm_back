//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <cstdint>
#include <string>

#include "common/common.h"
#include "dao/models/bms_task.h"
#include "dao/task_dao.h"
#include "qifeng_framework/common/logger.h"

namespace qifeng_ca {

    constexpr std::string_view TaskSelectSQL = "SELECT id, task_id, audio_id, account_id, task_type, task_status, "
                                               "task_phase, run_mode, timeout_ts, retry_count, error_message, "
                                               "task_data, create_time, update_time FROM bms_tasks";

    static void MapRowToTask(const soci::row &row, models::BmsTask &task) {
        task.mId = row.get<uint64_t>(0);
        task.mTaskId = row.get<std::string>(1);
        task.mAudioId = row.get<std::string>(2);
        task.mAccountId = row.get<uint64_t>(3);
        task.mTaskType = row.get<int>(4);
        task.mTaskStatus = row.get<int>(5);
        task.mTaskPhase = row.get<int>(6);
        task.mRunMode = row.get<int>(7);
        task.mTimeoutTs = row.get<uint64_t>(8);
        task.mRetryCount = row.get<int>(9);

        soci::indicator ind = row.get_indicator(10);
        if (ind == soci::i_ok) {
            task.mErrorMessage = row.get<std::string>(10);
        }
        ind = row.get_indicator(11);
        if (ind == soci::i_ok) {
            task.mTaskData = row.get<std::string>(11);
        }
        task.mCreateTime = row.get<int64_t>(12);
        task.mUpdateTime = row.get<int64_t>(13);
    }

    bool TaskDao::Insert(const models::BmsTask &task) {
        try {
            soci::session session = GetSession();
            session << "INSERT INTO bms_tasks (task_id, audio_id, account_id, "
                       "task_type, task_status, task_phase, run_mode, timeout_ts, "
                       "retry_count, error_message, task_data, create_time, update_time) "
                       "VALUES (:task_id, :audio_id, :account_id, "
                       ":task_type, :task_status, :task_phase, :run_mode, :timeout_ts, "
                       ":retry_count, :error_message, :task_data, :create_time, :update_time)",
                soci::use(task.mTaskId, "task_id"), soci::use(task.mAudioId, "audio_id"),
                soci::use(task.mAccountId, "account_id"), soci::use(task.mTaskType, "task_type"),
                soci::use(task.mTaskStatus, "task_status"), soci::use(task.mTaskPhase, "task_phase"),
                soci::use(task.mRunMode, "run_mode"), soci::use(task.mTimeoutTs, "timeout_ts"),
                soci::use(task.mRetryCount, "retry_count"), soci::use(task.mErrorMessage, "error_message"),
                soci::use(task.mTaskData, "task_data"), soci::use(task.mCreateTime, "create_time"),
                soci::use(task.mUpdateTime, "update_time");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "TaskDao::Insert failed: " << e.what();
            return false;
        }
    }

    models::BmsTask TaskDao::GetByTaskId(const std::string &taskId) {
        models::BmsTask task;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs = (session.prepare << std::string(TaskSelectSQL) << " WHERE task_id = :task_id",
                                          soci::use(taskId, "task_id"));
            for (const soci::row &row : rs) {
                MapRowToTask(row, task);
                break;
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "TaskDao::GetByTaskId failed: " << e.what();
        }
        return task;
    }

    std::vector<models::BmsTask> TaskDao::GetByAudioId(const std::string &audioId) {
        std::vector<models::BmsTask> tasks;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs =
                (session.prepare << std::string(TaskSelectSQL) << " WHERE audio_id = :audio_id",
                 soci::use(audioId, "audio_id"));
            for (const soci::row &row : rs) {
                models::BmsTask task;
                MapRowToTask(row, task);
                tasks.push_back(std::move(task));
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "TaskDao::GetByAudioId failed: " << e.what();
        }
        return tasks;
    }

    std::vector<models::BmsTask> TaskDao::GetPendingTasks() {
        std::vector<models::BmsTask> tasks;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs =
                (session.prepare << std::string(TaskSelectSQL)
                                 << " WHERE task_status NOT IN (3, 8) ORDER BY task_type ASC, create_time ASC");
            for (const soci::row &row : rs) {
                models::BmsTask task;
                MapRowToTask(row, task);
                tasks.push_back(std::move(task));
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "TaskDao::GetPendingTasks failed: " << e.what();
        }
        return tasks;
    }

    bool TaskDao::UpdateStatus(const std::string &taskId, int taskStatus, int taskPhase) {
        try {
            int64_t timeMs = static_cast<int64_t>(GetTimeMs());
            soci::session session = GetSession();
            session << "UPDATE bms_tasks SET task_status = :task_status, "
                       "task_phase = :task_phase, update_time = :update_time "
                       "WHERE task_id = :task_id",
                soci::use(taskStatus, "task_status"), soci::use(taskPhase, "task_phase"),
                soci::use(timeMs, "update_time"), soci::use(taskId, "task_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "TaskDao::UpdateStatus failed: " << e.what();
            return false;
        }
    }

    bool TaskDao::UpdateTaskData(const models::BmsTask &task) {
        try {
            int64_t timeMs = static_cast<int64_t>(GetTimeMs());
            soci::session session = GetSession();
            session << "UPDATE bms_tasks SET task_type = :task_type, "
                       "task_status = :task_status, task_phase = :task_phase, "
                       "run_mode = :run_mode, timeout_ts = :timeout_ts, "
                       "retry_count = :retry_count, error_message = :error_message, "
                       "task_data = :task_data, update_time = :update_time "
                       "WHERE task_id = :task_id",
                soci::use(task.mTaskType, "task_type"), soci::use(task.mTaskStatus, "task_status"),
                soci::use(task.mTaskPhase, "task_phase"), soci::use(task.mRunMode, "run_mode"),
                soci::use(task.mTimeoutTs, "timeout_ts"), soci::use(task.mRetryCount, "retry_count"),
                soci::use(task.mErrorMessage, "error_message"), soci::use(task.mTaskData, "task_data"),
                soci::use(timeMs, "update_time"), soci::use(task.mTaskId, "task_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "TaskDao::UpdateTaskData failed: " << e.what();
            return false;
        }
    }

    bool TaskDao::UpdateError(const std::string &taskId, int taskStatus, const std::string &errorMessage) {
        try {
            int64_t timeMs = static_cast<int64_t>(GetTimeMs());
            soci::session session = GetSession();
            session << "UPDATE bms_tasks SET task_status = :task_status, "
                       "error_message = :error_message, update_time = :update_time "
                       "WHERE task_id = :task_id",
                soci::use(taskStatus, "task_status"), soci::use(errorMessage, "error_message"),
                soci::use(timeMs, "update_time"), soci::use(taskId, "task_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "TaskDao::UpdateError failed: " << e.what();
            return false;
        }
    }

    bool TaskDao::IncrementRetryCount(const std::string &taskId) {
        try {
            int64_t timeMs = static_cast<int64_t>(GetTimeMs());
            soci::session session = GetSession();
            session << "UPDATE bms_tasks SET retry_count = retry_count + 1, "
                       "update_time = :update_time WHERE task_id = :task_id",
                soci::use(timeMs, "update_time"), soci::use(taskId, "task_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "TaskDao::IncrementRetryCount failed: " << e.what();
            return false;
        }
    }

    bool TaskDao::DeleteByTaskId(const std::string &taskId) {
        try {
            soci::session session = GetSession();
            session << "DELETE FROM bms_tasks WHERE task_id = :task_id", soci::use(taskId, "task_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "TaskDao::DeleteByTaskId failed: " << e.what();
            return false;
        }
    }

    bool TaskDao::DeleteCompletedTasks() {
        try {
            soci::session session = GetSession();
            session << "DELETE FROM bms_tasks WHERE task_status = 3";
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "TaskDao::DeleteCompletedTasks failed: " << e.what();
            return false;
        }
    }

}  // namespace qifeng_ca
