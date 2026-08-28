//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_VOICEPRINT_VOICEPRINT_SERVICE_H
#define QIFENG_CA_INCLUDE_CORE_VOICEPRINT_VOICEPRINT_SERVICE_H

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

#include "qifeng_ca/common.pb.h"
#include "qifeng_ca/voiceprint.pb.h"

#include "common/status.h"
#include "dao/speaker_dao.h"
#include "schedule/task/voiceprint_task.h"

namespace qifeng_ca {

    // 声纹录制请求上下文(用于回调时传递业务字段, 避免函数参数超过4个)
    struct VoiceprintRecordInfo {
        uint64_t mAccountId {0};
        std::string mNumber;
        std::string mSpeaker;
        std::string mRemark;
        void Reset() {
            mAccountId = 0;
            mNumber.clear();
            mSpeaker.clear();
            mRemark.clear();
        }
    };

    class VoiceprintService {
    public:
        VoiceprintService() = default;
        ~VoiceprintService() = default;

        VoiceprintService(const VoiceprintService &) = delete;
        VoiceprintService &operator=(const VoiceprintService &) = delete;
        VoiceprintService(VoiceprintService &&) noexcept = delete;
        VoiceprintService &operator=(VoiceprintService &&) = delete;

        Status RecordVoiceprint(const RecordVoiceprintRequest &req, RecordVoiceprintResponse* resp);

        // 回滚声纹录制: 取消当前录制任务, 不等待AAS结果
        Status RollbackVoiceprint(const RollbackVoiceprintRequest &req, Empty* resp);

        // 停止声纹录制并同步等待AAS声纹提取结果(1-2s超时)
        Status StopVoiceprint(const StopVoiceprintRequest &req, StopVoiceprintResponse* resp);

        Status CheckVoiceprintInfo(const CheckVoiceprintInfoRequest &req, Empty* resp);

        Status AddVoiceprint(const AddVoiceprintRequest &req, Empty* resp);

        Status GetVoiceprintList(const VoiceprintSearchRequest &req, VoiceprintSearchResponse* resp);

        Status EditVoiceprint(const EditVoiceprintRequest &req, Empty* resp);

        Status DeleteVoiceprint(const DeleteVoiceprintRequest &req, Empty* resp);

    private:
        void FillSearchResponse(const SpeakerSearchResult &result, VoiceprintSearchResponse* resp);

        // 超时/取消时的异步回调: 将声纹特征写入数据库
        void OnVoiceprintRecorded(const VoiceprintRecordInfo &info, const VoiceprintResult &result);

        // 分段声纹提取完成回调
        void OnSegmentVoiceprintResult(const VoiceprintRecordInfo &info, const VoiceprintResult &result);

        Status SaveVoiceprintToDb(const VoiceprintRecordInfo &info, const VoiceprintResult &result);

        // 返回 nullopt 表示任务处于活跃运行态, 调用方应继续走 StopAndWait 主流程;
        // 返回 Status 表示已处理, 调用方直接返回该状态.
        std::optional<Status> HandleInactiveTask(const std::shared_ptr<VoiceprintTask> &task);

        // 加锁清理 mActiveTask 与 mActiveRecordInfo
        void CleanupActiveTask();

    private:
        // 当前声纹录制任务(保留引用以支持StopVoiceprint的StopAndWait同步等待,
        // 任务本身通过PcmEngine::Submit提交到TaskScheduler统一调度)
        std::shared_ptr<VoiceprintTask> mActiveTask;
        std::mutex mTaskMutex;

        // 当前声纹录制的业务上下文(用于停止时写入数据库)
        VoiceprintRecordInfo mActiveRecordInfo;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_VOICEPRINT_VOICEPRINT_SERVICE_H
