#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <sys/types.h>
#include <thread>
#include <vector>

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/scope_exit.h"
#include "utf8/checked.h"

#include "common/audio/audio_file_converter.h"
#include "common/audio/audio_hal_utils.h"
#include "common/audio/audio_utils.h"
#include "common/audio_enums.h"
#include "common/common.h"
#include "common/config/meeting_config.h"
#include "common/device/disk_check.h"
#include "common/status.h"
#include "common/utils/file_name_generator.h"
#include "common/utils/file_opt.h"
#include "common/utils/symlink_manager.h"
#include "common/ws/system_message_notifier.h"
#include "core/meeting/meeting_access.h"
#include "core/meeting/meeting_check.h"
#include "core/meeting/recording_service.h"
#include "dao/models/bms_audio.h"
#include "dao/models/bms_summary.h"
#include "dao/models/bms_trans.h"
#include "dao_managers/meeting_dao_manager.h"
#include "dao_managers/user_dao_manager.h"
#include "internal/display_manager.h"
#include "internal/hal/hal_bridge.h"
#include "internal/recording_manager.h"
#include "schedule/pcm/pcm_engine.h"
#include "schedule/task/audio_read_task.h"
#include "schedule/task/file_realtime_trans_task.h"
#include "schedule/task/offline_trans_task.h"

namespace qifeng_ca {

    struct BuildAudioInfo {
        uint64_t mAccountId;
        uint64_t mNoteId;
        std::string mAudioId;
        std::string mWavPath;
    };

    static Status CreateWavFile(const std::string &wavPath) {
        // 创建占位WAV文件，确保目录和文件都存在
        FileOpt::CreateDstDirectory(wavPath);
        std::ofstream ofs(wavPath, std::ios::binary);
        if (!ofs) {
            SLOG_ERROR << "Service: failed to create placeholder wav file: " << wavPath;
            return Status {-1, "创建录音文件失败"};
        }

        // 获取麦克风采样率、声道数、位深
        AudioUtilsConfig config = HalBridge::GetInstance().GetAudioFormat();
        AudioUtils::WriteWavHeader(ofs, config);
        ofs.close();
        return Status {};
    }

    // 校验离线音频文件是否存在、大小合法、且为支持的音频格式(WAV/MP3)
    static bool ValidateOfflineAudioFile(const std::string &srcPath) {
        if (srcPath.empty() || !std::filesystem::exists(srcPath)) {
            SLOG_ERROR << "ValidateOfflineAudioFile: source file not exist, path=" << srcPath;
            return false;
        }
        if (!AudioFileConverter::IsSupportedAudioFile(srcPath)) {
            SLOG_ERROR << "ValidateOfflineAudioFile: unsupported format, path=" << srcPath;
            return false;
        }
        // WAV: 校验文件头; MP3: 仅校验文件非空
        if (AudioFileConverter::IsWavFile(srcPath)) {
            WavHeaderInfo headerInfo;
            if (!AudioUtils::ParseWavHeader(srcPath, headerInfo)) {
                SLOG_ERROR << "ValidateOfflineAudioFile: not a valid WAV file, path=" << srcPath;
                return false;
            }
        } else if (std::filesystem::file_size(srcPath) == 0) {
            SLOG_ERROR << "ValidateOfflineAudioFile: empty file, path=" << srcPath;
            return false;
        }
        return true;
    }

    static Status ValidateAddRequest(const AddRecordingRequest &req) {
        auto &cfg = MeetingConfig::GetInstance();
        if (req.theme().empty()) {
            return Status {-1, "会议主题不能为空"};
        }
        if (static_cast<size_t>(utf8::distance(req.theme().begin(), req.theme().end())) >
            static_cast<size_t>(cfg.GetMaxThemeLen())) {
            return Status {-1, "主题长度不能超过" + std::to_string(cfg.GetMaxThemeLen())};
        }
        if (!IsThemeContentValid(req.theme())) {
            return Status {-1, "主题只能包含中文、英文、数字和_-."};
        }
        if (req.has_moderator() && static_cast<size_t>(utf8::distance(req.moderator().begin(), req.moderator().end())) >
                                       static_cast<size_t>(cfg.GetMaxModeratorLen())) {
            return Status {-1, "主持人长度不能超过" + std::to_string(cfg.GetMaxModeratorLen())};
        }
        if (req.has_attendees() && static_cast<size_t>(utf8::distance(req.attendees().begin(), req.attendees().end())) >
                                       static_cast<size_t>(cfg.GetMaxAttendees())) {
            return Status {-1, "参会人长度不合法"};
        }
        if (req.has_places() && static_cast<size_t>(utf8::distance(req.places().begin(), req.places().end())) >
                                    static_cast<size_t>(cfg.GetMaxPlacesLen())) {
            return Status {-1, "地点长度不能超过" + std::to_string(cfg.GetMaxPlacesLen())};
        }
        if (req.has_remark() && static_cast<size_t>(utf8::distance(req.remark().begin(), req.remark().end())) >
                                    static_cast<size_t>(cfg.GetMaxRemarkLen())) {
            return Status {-1, "备注长度不能超过" + std::to_string(cfg.GetMaxRemarkLen())};
        }
        if (req.recording_time() < 0) {
            return Status {-1, "录音时间参数不合法"};
        }
        if (req.kind() < 0 || req.kind() > 10) {
            return Status {-1, "会议类型不合法"};
        }
        return Status {};
    }

    // 构建离线上传的Audio模型
    static models::Audio BuildUploadAudioModel(const BuildAudioInfo &info, const AddRecordingRequest &req) {
        models::Audio audio;
        audio.mAudioId = info.mAudioId;
        audio.mAccountId = info.mAccountId;
        audio.mSource = static_cast<int32_t>(AudioSource::UserUpload);
        audio.mIsRecording = static_cast<int32_t>(RecordingFlag::NotRecording);
        audio.mStatus = static_cast<int32_t>(AudioStatus::WaitTrans);
        audio.mFileName = info.mWavPath;
        audio.mNoteId = info.mNoteId;
        audio.mTheme = req.theme();
        if (req.has_moderator()) {
            audio.mModerator = req.moderator();
        }
        if (req.has_attendees()) {
            audio.mAttendees = req.attendees();
        }
        if (req.has_places()) {
            audio.mPlaces = req.places();
        }
        if (req.has_remark()) {
            audio.mRemark = req.remark();
        }
        audio.mRecordingTime = req.recording_time();
        auto totalTime = AudioUtils::CalculateTotalTimeFromWav(info.mWavPath);
        SLOG_INFO << "AddRecording: totalTime=" << totalTime << " from wavPath=" << info.mWavPath;
        audio.mTotalTime = totalTime;
        audio.mKind = req.kind();
        audio.mTimestamp = static_cast<int64_t>(GetTimeMs());
        audio.mUpdateTime = audio.mTimestamp;
        return audio;
    }

    // 删除临时文件列表(校验或处理失败时回滚)
    static void CleanupUploadedFiles(const std::vector<std::string> &filePaths) {
        for (const auto &path : filePaths) {
            if (!path.empty() && std::filesystem::exists(path)) {
                std::filesystem::remove(path);
            }
        }
    }

    // 第一阶段: 校验所有上传文件, 任一不合法则删除所有文件并返回失败
    static Status ValidateAllUploadedFiles(const AddRecordingRequest &req) {
        const auto &filePaths = req.file_paths();
        if (filePaths.empty()) {
            return Status {-1, "未指定上传文件"};
        }
        ScopeExit cleanupFiles([&]() { CleanupUploadedFiles({filePaths.begin(), filePaths.end()}); });
        Status validStatus = ValidateAddRequest(req);
        if (validStatus.GetCode() != 0) {
            return validStatus;
        }
        for (const auto &srcPath : filePaths) {
            if (!ValidateOfflineAudioFile(srcPath)) {
                // 校验失败, 删除所有已上传的临时文件
                return Status {-1, "音频文件校验失败"};
            }
        }
        cleanupFiles.Release();
        return {};
    }

    // 填充 AudioTransSummaryResponse 的转写列表(与 FillTransList 同语义, 复用 BuildDetailItems)
    static std::string FillTransSummaryList(const std::vector<models::Trans> &transList) {
        if (transList.empty()) {
            return "";
        }
        std::string trans;
        for (const auto &t : transList) {
            if (t.mContent.empty()) {
                continue;
            }
            trans += t.mContent;
        }
        return trans;
    }

    // 处理单个离线文件: 拷贝文件+创建DB记录+提交离线转写任务
    // 任一步骤失败则回滚已完成的操作(删除已拷贝的文件、已写入的DB记录)
    // TaskT为具体转写任务类型(OfflineTransTask / FileRealtimeTransTask)
    template <typename TaskT>
    static Status ProcessSingleUploadFile(uint64_t accountId, const std::string &srcPath,
                                          const AddRecordingRequest &req, AddRecordingResponse* resp) {
        std::string audioId = GenUUID();
        // 上传文件直接写到软连接目录(利用 /data2 大容量空间)
        std::string wavPath = GetSymlinkAudioFilePath(accountId, audioId);
        ScopeExit wavPathCleanup([&]() {
            FileOpt::RemoveFile(srcPath);
            FileOpt::RemoveFile(wavPath);
        });

        // 重采样前提前校验源文件时长, 避免对超长文件做无效的重采样转换
        int64_t srcDurationSec = AudioFileConverter::GetAudioDurationSeconds(srcPath);
        if (srcDurationSec < -1) {
            return Status {-1, "无法获取音频时长，请检查文件是否损坏"};
        }
        SLOG_INFO << "ProcessSingleUploadFile: src duration=" << srcDurationSec << "s, srcPath=" << srcPath;
        if (srcDurationSec > MeetingConfig::GetInstance().GetLimitRecordTimeSec()) {
            return Status {-1, "音频时长超过3小时限制，请缩短后重试"};
        }

        // 准备音频文件(格式匹配rename, 不匹配重采样)
        std::string taskFilePath = PrepareSingleAudioFile(srcPath, wavPath);
        if (taskFilePath.empty()) {
            return Status {-1, "音频文件处理失败"};
        }

        // 再次做校验，这里是校验文件头部信息是否正确（双重校验，确保文件完整性）
        int32_t durationMs = AudioUtils::CalculateTotalTimeFromWav(wavPath);
        int64_t durationSec = static_cast<int64_t>(durationMs) / 1000;
        SLOG_INFO << "ProcessSingleUploadFile: duration=" << durationSec << "s, wavPath=" << wavPath;
        if (durationSec > MeetingConfig::GetInstance().GetLimitRecordTimeSec()) {
            return Status {-1, "音频时长超过3小时限制，请缩短后重试"};
        }

        // 创建笔记记录
        uint64_t noteId {};
        if (!MeetingDaoManager::GetInstance().CreateNote(accountId, audioId, noteId)) {
            SLOG_WARN << "ProcessSingleUploadFile: create note for audio failed, audioId=" << audioId;
            return Status {-1, "笔记创建失败"};
        }

        // 创建音频记录
        auto info = BuildAudioInfo {
            .mAccountId = accountId,
            .mNoteId = noteId,
            .mAudioId = audioId,
            .mWavPath = wavPath,
        };
        auto audio = BuildUploadAudioModel(info, req);
        if (!MeetingDaoManager::GetInstance().Insert(audio)) {
            MeetingDaoManager::GetInstance().DeleteAudioWithRelations(accountId, audioId);
            return Status {-1, "创建音频记录失败"};
        }

        // 提交转写任务到TaskScheduler(任务类型由TaskT决定)
        auto task = std::make_shared<TaskT>(audioId, accountId, wavPath, wavPath);
        PcmEngine::GetInstance().Submit(task);

        wavPathCleanup.Release();
        resp->add_audio_ids(audioId);
        SLOG_INFO << "ProcessSingleUploadFile: audioId=" << audioId << ", wavPath=" << wavPath;
        return {};
    }

    Status RecordingService::AddRecording(const AddRecordingRequest &req, AddRecordingResponse* resp) {
        // AddRecording统一入口: 根据is_rel_time区分实时/离线
        // 推荐通过独立接口调用: AddRecording(实时) / UploadRecording(离线上传)
        if (req.is_rel_time()) {
            return StartRealtimeRecording(req, resp);
        }
        return UploadOfflineRecording(req, resp);
    }

    // 显示屏最好是在所有流程完成后再控制
    Status RecordingService::StartRealtimeRecording(const AddRecordingRequest &req, AddRecordingResponse* resp) {
        Status validStatus = ValidateAddRequest(req);
        if (validStatus.GetCode() != 0) {
            return validStatus;
        }
        if (RecordingManager::GetInstance().IsRecording() || HalBridge::GetInstance().IsRecordingActive()) {
            SetupFingerprintLed();
            return {-1, "当前有正在录音的会议"};
        }
        Status diskStatus = DiskCheck::GetInstance().IsDiskLow(req.account_id());
        if (!diskStatus.IsSuccess()) {
            StopFingerprintLed();
            return diskStatus;
        }
        auto user = UserDaoManager::GetInstance().GetByAccountId(req.account_id());
        std::string sponsor = user.mGroupId == 3 ? "访客" : user.mUserName;
        auto status = HalStartAudio(req.theme(), sponsor, user.mGroupId == 3 ? AudioType::Public : AudioType::Private);
        if (!status.IsSuccess()) {
            return status;
        }
        ScopeExit recordingFunc([]() { HalStopAudio(); });

        // 创建音频文件和数据库记录
        std::string audioId = GenUUID();
        Status dbStatus = CreateAudioRecord(req, audioId);
        if (!dbStatus.IsSuccess()) {
            return dbStatus;
        }
        RecordingManager::GetInstance().StartRecording(audioId, req.account_id());

        // 提交录音任务到TaskScheduler
        auto readTask = std::make_shared<AudioReadTask>(audioId, req.account_id());
        // 私密会议不发送通知
        if (req.account_id() == GetGuestAccountId()) {
            readTask->EnableSystemMessageNotifier();
            SystemMessageNotifier::GetInstance().SendStartRecording(audioId);
        }
        PcmEngine::GetInstance().Submit(readTask);

        resp->add_audio_ids(audioId);
        recordingFunc.Release();
        SLOG_INFO << "AddRecording: realtime recording submitted, audioId=" << audioId;
        return Status {};
    }

    Status RecordingService::UploadOfflineRecording(const AddRecordingRequest &req, AddRecordingResponse* resp) {
        // 先校验所有文件, 任一失败则删除所有文件
        Status validStatus = ValidateAllUploadedFiles(req);
        if (validStatus.GetCode() != 0) {
            return validStatus;
        }

        // 所有文件校验通过, 逐个处理(重采样+创建记录+提交离线转写任务)
        auto accountId = req.account_id();
        for (const auto &srcPath : req.file_paths()) {
            Status procStatus = ProcessSingleUploadFile<OfflineTransTask>(accountId, srcPath, req, resp);
            if (procStatus.GetCode() != 0) {
                SLOG_ERROR << "UploadOfflineRecording: process file failed, path=" << srcPath;
                return procStatus;
            }
        }
        return Status {};
    }

    // 兜底校准: 以WAV文件实测时长更新total_time(按实际文件大小计算, 不依赖任务回填WAV头)
    static void CalibrateTotalTimeByFile(const models::Audio &audio) {
        std::string wavPath = SymlinkManager::GetInstance().ResolveData2Path(audio.mFileName);
        int32_t fileTimeMs = AudioUtils::CalculateTotalTimeFromWav(wavPath);
        if (fileTimeMs <= 0) {
            return;  // 文件不存在或解析失败: 保留DB原值
        }
        auto &daoMg = MeetingDaoManager::GetInstance();
        if (daoMg.UpdateTotalTime(audio.mAccountId, audio.mAudioId, fileTimeMs)) {
            SLOG_INFO << "RecordStop: total_time calibrated by file, audioId=" << audio.mAudioId
                      << " totalTimeMs=" << fileTimeMs << " prev=" << audio.mTotalTime;
        }
    }

    Status RecordingService::RecordStop(const RecordStopRequest &req, Empty* resp) {
        (void)resp;
        if (req.audio_id().empty()) {
            return Status {-1, "录音audio_id不能为空"};
        }

        auto [audio, accessStatus] = GetAccessibleAudio(req.account_id(), req.audio_id());
        if (!accessStatus.IsSuccess()) {
            // 无权结束会议: 显示屏弹窗提示(operationTip=2, 2秒后自动消失)
            if (RecordingManager::GetInstance().IsRecording()) {
                DisplayManager::GetInstance().ShowPopup(2, 2000);
                DisplayManager::GetInstance().RefreshMeetingPage();
            }
            return accessStatus;
        }

        if (audio.mIsRecording != static_cast<int32_t>(RecordingFlag::Recording)) {
            return Status {-1, "该音频没有录制"};
        }

        HalStopAudio();

        // 更新数据库: 录音结束, 状态设为待总结, 同时清除录制标记
        MeetingDaoManager::GetInstance().UpdateStatus(audio.mAccountId, req.audio_id(),
                                                      static_cast<int>(AudioStatus::WaitSummary),
                                                      static_cast<int>(RecordingFlag::NotRecording));

        // 更新录音管理器状态

        RecordingManager::GetInstance().StopRecording(req.audio_id());

        // 轮询查询，尽可能保证stop返回后任务已完成
        constexpr int pollIntervalMs = 50;
        constexpr int maxWaitMs = 500;
        auto &pcm = PcmEngine::GetInstance();
        for (int waitedMs = 0; waitedMs < maxWaitMs && pcm.IsRunning(req.audio_id()); waitedMs += pollIntervalMs) {
            std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs));
        }

        // 兜底: 按文件实测时长校准total_time(正常路径由AudioReadTask在OnRecordingStopped写入准确值)
        CalibrateTotalTimeByFile(audio);

        SLOG_INFO << "RecordStop: recording stopped, audioId=" << req.audio_id();
        return Status {};
    }

    Status RecordingService::PauseRecording(const RecordStopRequest &req, Empty* resp) {
        (void)resp;
        if (req.audio_id().empty()) {
            return Status {-1, "录音audio_id不能为空"};
        }

        auto [audio, accessStatus] = GetAccessibleAudio(req.account_id(), req.audio_id());
        if (!accessStatus.IsSuccess()) {
            return accessStatus;
        }

        if (audio.mIsRecording != static_cast<int32_t>(RecordingFlag::Recording)) {
            return Status {-1, "该音频没有录制"};
        }
        if (audio.mStatus != static_cast<int>(AudioStatus::Meeting)) {
            return Status {-1, "当前不在录音中"};
        }

        auto status = HalPauseAudio(req.type());
        if (!status.IsSuccess()) {
            return status;
        }

        RecordingManager::GetInstance().SetPaused(true);

        MeetingDaoManager::GetInstance().UpdateStatus(audio.mAccountId, req.audio_id(),
                                                      static_cast<int>(AudioStatus::Paused),
                                                      std::string(AudioStatusMsg::RecordingPaused));

        SLOG_INFO << "PauseRecording: recording paused, audioId=" << req.audio_id();
        return Status {};
    }

    Status RecordingService::ResumeRecording(const RecordStopRequest &req, Empty* resp) {
        (void)resp;
        if (req.audio_id().empty()) {
            return Status {-1, "录音audio_id不能为空"};
        }

        auto [audio, accessStatus] = GetAccessibleAudio(req.account_id(), req.audio_id());
        if (!accessStatus.IsSuccess()) {
            return accessStatus;
        }

        if (audio.mStatus != static_cast<int>(AudioStatus::Paused)) {
            return Status {-1, "当前录音未暂停"};
        }

        auto status = HalResumeAudio(req.type());
        if (!status.IsSuccess()) {
            return status;
        }

        // 清除RecordingManager暂停状态(DoReadCycle检测后恢复AAS provider)
        RecordingManager::GetInstance().SetPaused(false);

        // 更新DB状态为录音中
        MeetingDaoManager::GetInstance().UpdateStatus(audio.mAccountId, req.audio_id(),
                                                      static_cast<int>(AudioStatus::Meeting),
                                                      std::string(AudioStatusMsg::RecordingTranscribing));

        SLOG_INFO << "ResumeRecording: recording resumed, audioId=" << req.audio_id();
        return Status {};
    }

    Status RecordingService::CreateAudioRecord(const AddRecordingRequest &req, const std::string &audioId) {
        auto accountId = req.account_id();
        std::string wavPath = GetAudioFilePath(accountId, audioId);
        RecordingManager::GetInstance().SetWavFilePath(wavPath);

        // 创建普通WAV文件(在data目录)和符号链接WAV文件(在data/src目录)
        std::string srcWavPath = SymlinkManager::GetInstance().ToSymlinkPath(wavPath);
        Status status = CreateWavFile(wavPath);
        Status srcStatus = CreateWavFile(srcWavPath);
        SLOG_DEBUG << "CreateAudioRecord: created wav file, path=" << wavPath << " srcPath=" << srcWavPath;
        if (!status.IsSuccess() || !srcStatus.IsSuccess()) {
            return status.IsSuccess() ? srcStatus : status;
        }

        models::Audio audio;
        audio.mAudioId = audioId;
        audio.mAccountId = accountId;
        audio.mSource = static_cast<int32_t>(AudioSource::DeviceCollect);
        audio.mIsRecording = static_cast<int32_t>(RecordingFlag::Recording);
        audio.mStatus = static_cast<int32_t>(AudioStatus::Meeting);
        audio.mFileName = wavPath;
        audio.mTheme = req.theme();
        if (req.has_moderator()) {
            audio.mModerator = req.moderator();
        }
        if (req.has_attendees()) {
            audio.mAttendees = req.attendees();
        }
        if (req.has_places()) {
            audio.mPlaces = req.places();
        }
        if (req.has_remark()) {
            audio.mRemark = req.remark();
        }
        audio.mRecordingTime = req.recording_time();
        audio.mTotalTime = 0;
        audio.mKind = req.kind();
        audio.mTimestamp = static_cast<int64_t>(GetTimeMs());
        audio.mUpdateTime = audio.mTimestamp;

        if (!MeetingDaoManager::GetInstance().CreateNote(accountId, audioId, audio.mNoteId)) {
            SLOG_WARN << "CreateAudioRecord: create note for audio failed, audioId=" << audioId;
            return Status {-1, "笔记创建失败"};
        }
        if (!MeetingDaoManager::GetInstance().Insert(audio)) {
            return Status {-1, "创建音频记录失败"};
        }

        return status;
    }

    // ---------------------- 测试相关接口 START -------------------------
    Status RecordingService::UploadRealtimeLikeRecording(const AddRecordingRequest &req, AddRecordingResponse* resp) {
        // 复用离线上传的文件校验逻辑(格式/大小/可读性)
        Status validStatus = ValidateAllUploadedFiles(req);
        if (validStatus.GetCode() != 0) {
            return validStatus;
        }

        // 所有文件校验通过, 逐个处理(重采样+创建记录+提交文件模拟实时转写任务)
        // 与UploadOfflineRecording仅任务类型不同, 复用ProcessSingleUploadFile模板
        auto accountId = req.account_id();
        for (const auto &srcPath : req.file_paths()) {
            Status procStatus = ProcessSingleUploadFile<FileRealtimeTransTask>(accountId, srcPath, req, resp);
            if (procStatus.GetCode() != 0) {
                SLOG_ERROR << "UploadRealtimeLikeRecording: process file failed, path=" << srcPath;
                return procStatus;
            }
        }
        return Status {};
    }

    Status RecordingService::GetAudioTransSummary(const AudioTransSummaryRequest &req,
                                                  AudioTransSummaryResponse* resp) {
        if (req.audio_id().empty()) {
            return Status {-1, "录音audio_id不能为空"};
        }
        if (!req.include_trans() && !req.include_summary()) {
            return Status {-1, "未指定需要查询的内容"};
        }

        auto [audio, accessStatus] = GetAccessibleAudio(req.account_id(), req.audio_id());
        if (!accessStatus.IsSuccess()) {
            return accessStatus;
        }

        std::vector<models::Trans> transList;
        models::Summary summary;
        MeetingDaoManager::GetInstance().GetRecordingDetailData(audio.mAccountId, audio.mAudioId, transList, summary);

        bool isSummaryReady = false;
        bool isTransReady = false;
        auto audioStatus = static_cast<AudioStatus>(audio.mStatus);
        isSummaryReady = (audioStatus == AudioStatus::SummaryComplete || audioStatus == AudioStatus::TransException ||
                          audioStatus == AudioStatus::PermanentFailed);
        // 未转写完
        isTransReady = !(audioStatus == AudioStatus::WaitTrans || audioStatus == AudioStatus::Transing);

        if (req.include_summary() && !summary.mContent.empty()) {
            resp->mutable_summary()->set_txt(summary.mContent);
        }
        if (req.include_trans()) {
            resp->mutable_transform()->set_txt(std::move(FillTransSummaryList(transList)));
        }

        resp->mutable_summary()->set_ready(isSummaryReady);
        resp->mutable_transform()->set_ready(isTransReady);
        return Status {};
    }
    // ---------------------- 测试相关接口 END -------------------------

}  // namespace qifeng_ca