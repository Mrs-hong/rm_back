//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//
#include <cstdint>

#include "qifeng_framework/common/logger.h"

#include "common/audio/audio_utils.h"
#include "common/audio_enums.h"
#include "common/config/meeting_config.h"
#include "common/utils/file_name_generator.h"
#include "common/utils/symlink_manager.h"
#include "core/meeting/meeting_db_service.h"
#include "dao/audio_dao.h"
#include "dao_managers/meeting_dao_manager.h"
#include "schedule/pcm/task_recoverer.h"
#include "schedule/task/audio_read_task.h"
#include "schedule/task/offline_trans_task.h"
#include "schedule/task/summary_task.h"

namespace qifeng_ca {

    namespace {

        // WAV实测结果: 解析失败时totalTimeMs无效, 不得以0覆盖DB中的total_time
        enum class WavMeasure { Ok, TooShort, ParseFailed };

        // 按WAV文件实测时长判断录音是否过短(文件大小兜底头部dataSize, 未回填头的文件也能测准)
        WavMeasure MeasureWavDuration(const std::string &wavPath, int32_t &totalTimeMs) {
            WavHeaderInfo wavInfo;
            if (!AudioUtils::ParseWavHeader(wavPath, wavInfo)) {
                return WavMeasure::ParseFailed;
            }
            AudioUtilsConfig wavCfg {static_cast<uint32_t>(wavInfo.mSampleRate),
                                     static_cast<uint16_t>(wavInfo.mChannels),
                                     static_cast<uint16_t>(wavInfo.mBitDepth)};
            totalTimeMs = AudioUtils::CalculateDurationMs(wavInfo.mDataSize, wavCfg);
            int32_t minMs = MeetingConfig::GetInstance().GetMinRecordingSeconds() * 1000;
            return totalTimeMs < minMs ? WavMeasure::TooShort : WavMeasure::Ok;
        }

        // 尝试修复未正常关闭的WAV文件
        void TryRepairWav(const std::string &audioId, std::string &wavPath) {
            bool repaired = AudioReadTask::RepairWavFile(wavPath);
            if (!repaired) {
                SLOG_ERROR << "TaskRecoverer: repair wav file failed, audioId=" << audioId;
            }
        }

        // 恢复中断的录音: 修复WAV -> 检查时长 -> 清理软连接文件 -> 迁移至软连接目录 -> 转为离线转写任务
        // (中断的录音无法继续, 统一转为离线转写任务重新处理)
        void RecoverSingleRecording(const models::Audio &audio, std::vector<TaskPtr> &out) {
            const auto &audioId = audio.mAudioId;
            const auto accountId = audio.mAccountId;
            SLOG_INFO << "TaskRecoverer: recovering interrupted recording, audioId=" << audioId;

            std::string wavPath = audio.mFileName;
            TryRepairWav(audioId, wavPath);

            int32_t totalTimeMs = 0;
            auto measure = MeasureWavDuration(wavPath, totalTimeMs);
            if (measure == WavMeasure::TooShort) {
                SLOG_INFO << "TaskRecoverer: recording too short, deleting, audioId=" << audioId
                          << ", totalTimeMs=" << totalTimeMs;
                auto ret = MeetingDBService::DeleteAudioCascade(accountId, audioId);
                SLOG_DEBUG << "TaskRecoverer: delete audio cascade, audioId=" << audioId << ", ret=" << ret.ToString();
                return;
            }

            // 将 data 文件迁移到 data2: data2 文件已存在且大小一致则复用, 否则删除后重新复制
            auto &symMgr = SymlinkManager::GetInstance();
            if (symMgr.IsData2Available() && symMgr.IsSymlinkValid()) {
                std::string relPath = GetAudioFilePath(accountId, audioId);
                std::string dstPath = symMgr.MigrateToSymlinkIfSameSize(wavPath, relPath);
                if (!dstPath.empty()) {
                    SLOG_INFO << "TaskRecoverer: migrated to symlink, audioId=" << audioId;
                    // 更新DB file_name为软连接路径
                    auto &daoMg = MeetingDaoManager::GetInstance();
                    daoMg.UpdateFileName(accountId, audioId, dstPath);
                    // 更新wavPath指向已迁移的文件(后续任务使用)
                    wavPath = dstPath;
                }
            }

            // 状态重置为等待转写(统一通过MeetingDaoManager访问DB, 不直接使用AudioDao)
            auto &daoMg = MeetingDaoManager::GetInstance();
            // WAV解析失败时保留DB原total_time, 不以0覆盖(避免列表接口字段缺失/详情显示00:00:00)
            if (measure == WavMeasure::Ok) {
                daoMg.UpdateTotalTime(accountId, audioId, totalTimeMs);
            } else {
                SLOG_WARN << "TaskRecoverer: parse wav failed, keep original total_time, audioId=" << audioId
                          << ", wavPath=" << wavPath;
            }
            daoMg.UpdateStatus(accountId, audioId, static_cast<int>(AudioStatus::WaitTrans),
                               static_cast<int>(RecordingFlag::NotRecording));
            daoMg.UpdateStatus(accountId, audioId, static_cast<int>(AudioStatus::WaitTrans),
                               std::string(AudioStatusMsg::RecoverToOfflineTrans));

            std::string taskFilePath = GetAudioFilePath(accountId, audioId);
            // 文件可能已迁移到data2(symlink), 需解析为有效路径
            taskFilePath = SymlinkManager::GetInstance().ResolveData2Path(taskFilePath);
            out.push_back(std::make_shared<OfflineTransTask>(audioId, accountId, wavPath, taskFilePath, true));
            SLOG_INFO << "TaskRecoverer: interrupted recording converted to offline, audioId=" << audioId;
        }

        void RecoverInterruptedRecording(std::vector<TaskPtr> &out) {
            AudioDao audioDao;
            AudioSearchFilter filter;
            filter.mIgnoreAccountId = true;
            filter.mPageSize = 1000;
            filter.mStatusList = {static_cast<int>(AudioStatus::Meeting), static_cast<int>(AudioStatus::Paused)};
            auto result = audioDao.Search(filter);
            if (result.mRecords.empty()) {
                return;
            }
            SLOG_INFO << "TaskRecoverer: found " << result.mRecords.size() << " interrupted recordings";
            for (const auto &audio : result.mRecords) {
                RecoverSingleRecording(audio, out);
            }
        }

        // 恢复中断的转写: 重置状态 -> 创建离线转写任务(重启转写, 清理旧记录)
        void RecoverSingleTrans(const models::Audio &audio, std::vector<TaskPtr> &out) {
            const auto &audioId = audio.mAudioId;
            const auto accountId = audio.mAccountId;
            SLOG_INFO << "TaskRecoverer: recovering interrupted trans, audioId=" << audioId;

            auto &daoMg = MeetingDaoManager::GetInstance();
            daoMg.UpdateStatus(accountId, audioId, 0);
            daoMg.UpdatePlanFinishTime(accountId, audioId, 0);  // 重置时间
            daoMg.UpdateStatus(accountId, audioId, static_cast<int>(AudioStatus::WaitTrans),
                               std::string(AudioStatusMsg::RecoverRestartOfflineTrans));

            std::string taskFilePath = GetAudioFilePath(accountId, audioId);
            // 文件可能已迁移到data2(symlink), 需解析为有效路径
            taskFilePath = SymlinkManager::GetInstance().ResolveData2Path(taskFilePath);
            // srcFilePath也需解析, audio.mFileName可能为data/下路径(旧数据)
            std::string srcFilePath = SymlinkManager::GetInstance().ResolveData2Path(audio.mFileName);
            out.push_back(std::make_shared<OfflineTransTask>(audioId, accountId, srcFilePath, taskFilePath, true));
            SLOG_INFO << "TaskRecoverer: interrupted trans converted to offline, audioId=" << audioId;
        }

        void RecoverInterruptedTrans(std::vector<TaskPtr> &out) {
            AudioDao audioDao;
            AudioSearchFilter filter;
            filter.mIgnoreAccountId = true;
            filter.mPageSize = 1000;
            filter.mStatusList = {
                static_cast<int>(AudioStatus::WaitTrans),
                static_cast<int>(AudioStatus::Transing),
            };
            auto result = audioDao.Search(filter);
            if (result.mRecords.empty()) {
                return;
            }
            SLOG_INFO << "TaskRecoverer: found " << result.mRecords.size() << " interrupted trans tasks";
            for (const auto &audio : result.mRecords) {
                RecoverSingleTrans(audio, out);
            }
        }

        // 恢复中断的纪要: 重置状态 -> 创建总结任务
        void RecoverSingleSummary(const models::Audio &audio, std::vector<TaskPtr> &out) {
            const auto &audioId = audio.mAudioId;
            const auto accountId = audio.mAccountId;
            SLOG_INFO << "TaskRecoverer: recovering interrupted summary, audioId=" << audioId;

            auto &daoMg = MeetingDaoManager::GetInstance();
            daoMg.UpdateStatus(accountId, audioId, 0);
            daoMg.UpdateSumProgress(accountId, audioId, 0, 0);  // 重置时间和进度
            daoMg.UpdateStatus(accountId, audioId, static_cast<int>(AudioStatus::WaitSummary),
                               std::string(AudioStatusMsg::RecoverRestartSummary));

            out.push_back(std::make_shared<SummaryTask>(audioId, accountId));
            SLOG_INFO << "TaskRecoverer: interrupted summary rescheduled, audioId=" << audioId;
        }

        void RecoverInterruptedSummary(std::vector<TaskPtr> &out) {
            AudioDao audioDao;
            AudioSearchFilter filter;
            filter.mIgnoreAccountId = true;
            filter.mPageSize = 1000;
            filter.mStatusList = {
                static_cast<int>(AudioStatus::WaitSummary),
                static_cast<int>(AudioStatus::Summarying),
            };
            auto result = audioDao.Search(filter);
            if (result.mRecords.empty()) {
                return;
            }
            SLOG_INFO << "TaskRecoverer: found " << result.mRecords.size() << " interrupted summary tasks";
            for (const auto &audio : result.mRecords) {
                RecoverSingleSummary(audio, out);
            }
        }

    }  // namespace

    std::vector<TaskPtr> TaskRecoverer::RecoverFromMeetingDb() {
        std::vector<TaskPtr> tasks;
        // 按恢复优先级: 录音 -> 转写 -> 纪要
        // (录音中断需先修复WAV并转为离线转写, 转写和纪要直接重建任务)
        RecoverInterruptedRecording(tasks);
        RecoverInterruptedTrans(tasks);
        RecoverInterruptedSummary(tasks);
        return tasks;
    }

}  // namespace qifeng_ca
