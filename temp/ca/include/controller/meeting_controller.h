//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CONTROLLER_MEETING_CONTROLLER_H
#define QIFENG_CA_INCLUDE_CONTROLLER_MEETING_CONTROLLER_H

#include "common/audit_action_registry.h"
#include "drogon/DrObject.h"
#include "qifeng_ca/common.pb.h"
#include "qifeng_ca/meeting.pb.h"
#include "qifeng_ca/user.pb.h"

#include "common/http/http.h"
#include "common/status.h"
#include "core/meeting/meeting_db_service.h"
#include "core/meeting/recording_download_service.h"
#include "core/meeting/recording_service.h"
#include "core/meeting/refresh_summary_service.h"

namespace qifeng_ca {

    class MeetingController final : public drogon::DrObject<MeetingController> {
    public:
        QIFENG_CA_METHOD_LIST_BEGIN(MeetingController);

        QIFENG_CA_METHOD_PREREQ_ADD(ActionDesc("获取录音列表", false), GetRecordingList,
                                    BmsPreAccountIdReq<MeetingSearchRequest>, "/web/meeting/getRecordingList",
                                    drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD(ActionDesc("获取录音详情", false), GetRecordingDetail,
                                    BmsPreAccountIdReq<MeetingDetailRequest>, "/web/meeting/getRecordingDetail",
                                    drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("编辑录音", EditRecording, BmsPreAccountIdReq<MeetingEditRequest>,
                                    "/web/meeting/editRecording", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("删除录音", DeleteRecording, BmsPreAccountIdReq<MeetingDeleteRequest>,
                                    "/web/meeting/delRecording", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD(ActionDesc("获取录音总时长", false), GetTotalTime,
                                    BmsPreAccountIdReq<AccountRequst>, "/web/meeting/getTotalTime", drogon::Get);

        QIFENG_CA_METHOD_PREREQ_ADD("更新打点标记", UpdateMarkers, BmsPreAccountIdReq<MarkerUpdateRequest>,
                                    "/web/meeting/updateMarkers", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("开始录音", AddRecording, BmsPreAccountIdReq<AddRecordingRequest>,
                                    "/web/meeting/addRecording", drogon::Post, "CADurationFilter");

        QIFENG_CA_METHOD_PREREQ_ADD("上传录音", UploadRecording, BmsPreUploadFileReq, "/web/meeting/uploadRecording",
                                    drogon::Post, "CADiskFilter", "CADurationFilter");

        QIFENG_CA_METHOD_PREREQ_ADD("停止录音", RecordStop, BmsPreAccountIdReq<RecordStopRequest>,
                                    "/web/meeting/recordStop", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("暂停录音", PauseRecording, BmsPreAccountIdReq<RecordStopRequest>,
                                    "/web/meeting/pauseRecording", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("继续录音", ResumeRecording, BmsPreAccountIdReq<RecordStopRequest>,
                                    "/web/meeting/resumeRecording", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD(ActionDesc("获取活跃录音", false), GetActiveRecording,
                                    BmsPreAccountIdReq<AccountRequst>, "/web/meeting/getActiveRecording", drogon::Get);

        QIFENG_CA_METHOD_PREREQ_ADD(ActionDesc("查询录音详情", false), GetRecordInfo,
                                    BmsPreAccountIdReq<RecordInfoRequest>, "/web/meeting/recordInfo", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_AFTERRESP_ADD("下载录音文件", DownloadAudio, BmsPreAccountIdReq<RecordInfoRequest>,
                                              BmsConvertXAccelRedirectResp, "/web/meeting/audioDownload", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("重置转写任务", ResetTranscribeTask, BmsPreAccountIdReq<ResetTranscribeTaskRequest>,
                                    "/web/meeting/resetTranscribeTask", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("公文预览", SummaryDocumentPreview,
                                    BmsPreAccountIdReq<SummaryDocumentPreviewRequest>,
                                    "/web/meeting/summarydocumentPreview", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("下载会议内容", RecordDownload, BmsPreAccountIdReq<RecordDownloadRequest>,
                                    "/web/meeting/recordDownload", drogon::Post, "CADiskFilter");

        QIFENG_CA_METHOD_PREREQ_ADD("保存转写数据", SaveTransformList, BmsPreAccountIdReq<SaveTransformRequest>,
                                    "/web/meeting/saveTransformList", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("保存会议纪要", SaveRecordingSummary, BmsPreAccountIdReq<SaveSummaryRequest>,
                                    "/web/meeting/saveRecordingSummary", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("重新生成会议纪要", RefreshSummary, BmsPreRefreshSummaryReq,
                                    "/web/meeting/refreshSummary", drogon::Post);

        QIFENG_CA_METHOD_ADD_NO_FILTER(ActionDesc("获取会议类型列表", false), GetSummaryKinds,
                                       "/web/meeting/getSummaryKinds", drogon::Get);

        QIFENG_CA_METHOD_PREREQ_ADD(ActionDesc("查询音频转写与纪要", false), GetAudioTransSummary,
                                    BmsPreAccountIdReq<AudioTransSummaryRequest>, "/web/meeting/getAudioTransSummary",
                                    drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD(ActionDesc("上传文件实时转写", false), UploadRealtimeLikeRecording,
                                    BmsPreUploadFileReq, "/web/meeting/uploadRealtimeLikeRecording", drogon::Post,
                                    "CADiskFilter", "CADurationFilter");

        QIFENG_CA_METHOD_LIST_END;

        Status GetRecordingList(const MeetingSearchRequest &req, MeetingSearchResponse &resp);

        Status GetRecordingDetail(const MeetingDetailRequest &req, MeetingDetailResponse &resp);

        Status EditRecording(const MeetingEditRequest &req, Empty &resp);

        Status DeleteRecording(const MeetingDeleteRequest &req, Empty &resp);

        Status GetTotalTime(const AccountRequst &req, MeetingTotalTimeResponse &resp);

        Status UpdateMarkers(const MarkerUpdateRequest &req, Empty &resp);

        Status AddRecording(const AddRecordingRequest &req, AddRecordingResponse &resp);

        Status UploadRecording(const AddRecordingRequest &req, AddRecordingResponse &resp);

        Status RecordStop(const RecordStopRequest &req, Empty &resp);

        Status PauseRecording(const RecordStopRequest &req, Empty &resp);

        Status ResumeRecording(const RecordStopRequest &req, Empty &resp);

        Status GetActiveRecording(const AccountRequst &req, GetActiveRecordingResponse &resp);

        Status GetRecordInfo(const RecordInfoRequest &req, RecordInfoResponse &resp);

        Status DownloadAudio(const RecordInfoRequest &req, RecordDownloadResponse &resp);

        Status ResetTranscribeTask(const ResetTranscribeTaskRequest &req, Empty &resp);

        Status SummaryDocumentPreview(const SummaryDocumentPreviewRequest &req, SummaryDocumentPreviewResponse &resp);

        Status RecordDownload(const RecordDownloadRequest &req, RecordDownloadResponse &resp);

        Status SaveTransformList(const SaveTransformRequest &req, SaveTransformResponse &resp);

        Status SaveRecordingSummary(const SaveSummaryRequest &req, Empty &resp);

        Status RefreshSummary(const RefreshSummaryRequest &req, RefreshSummaryResponse &resp);

        Status GetSummaryKinds(const Empty &req, GetSummaryKindResponse &resp);

        Status GetAudioTransSummary(const AudioTransSummaryRequest &req, AudioTransSummaryResponse &resp);

        Status UploadRealtimeLikeRecording(const AddRecordingRequest &req, AddRecordingResponse &resp);

    private:
        MeetingDBService mMeetingDBService;
        RefreshSummaryService mRefreshSummaryService;
        RecordingDownloadService mRecordingDownloadService;
        RecordingService mRecordingService;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CONTROLLER_MEETING_CONTROLLER_H
