//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_MEETING_RECORDING_SERVICE_H
#define QIFENG_CA_INCLUDE_CORE_MEETING_RECORDING_SERVICE_H

#include "qifeng_ca/common.pb.h"
#include "qifeng_ca/meeting.pb.h"

#include "common/status.h"

namespace qifeng_ca {

    class RecordingService {
    public:
        RecordingService() = default;
        ~RecordingService() = default;

        RecordingService(const RecordingService &) = delete;
        RecordingService &operator=(const RecordingService &) = delete;
        RecordingService(RecordingService &&) noexcept = delete;
        RecordingService &operator=(RecordingService &&) = delete;

        Status AddRecording(const AddRecordingRequest &req, AddRecordingResponse* resp);

        // 实时录音: 启动HAL录音+创建记录+提交录音任务
        Status StartRealtimeRecording(const AddRecordingRequest &req, AddRecordingResponse* resp);

        // 离线上传录音: 校验音频格式+重采样+创建记录+提交离线转写任务
        Status UploadOfflineRecording(const AddRecordingRequest &req, AddRecordingResponse* resp);

        Status RecordStop(const RecordStopRequest &req, Empty* resp);

        // 暂停/继续实时录音: 关闭/重启HAL音频接收, 暂停/恢复AAS, 更新DB状态
        Status PauseRecording(const RecordStopRequest &req, Empty* resp);

        Status ResumeRecording(const RecordStopRequest &req, Empty* resp);

        // 文件模拟实时转写上传: 复用离线上传校验, 但提交FileRealtimeTransTask(实时provider, 2s推送)
        Status UploadRealtimeLikeRecording(const AddRecordingRequest &req, AddRecordingResponse* resp);

        // 查询音频转写与纪要结果(可选返回转写列表/纪要文本)
        Status GetAudioTransSummary(const AudioTransSummaryRequest &req, AudioTransSummaryResponse* resp);

    private:
        Status CreateAudioRecord(const AddRecordingRequest &req, const std::string &audioId);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_MEETING_RECORDING_SERVICE_H
