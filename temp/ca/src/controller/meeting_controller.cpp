//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "qifeng_framework/common/logger.h"

#include "controller/meeting_controller.h"
namespace qifeng_ca {

    Status MeetingController::GetRecordingList(const qifeng_ca::MeetingSearchRequest &req,
                                               qifeng_ca::MeetingSearchResponse &resp) {
        SLOG_DEBUG << "GetRecordingList - current: " << req.current() << ", pageSize: " << req.page_size();

        Status status = mMeetingDBService.GetRecordingList(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status MeetingController::GetRecordingDetail(const qifeng_ca::MeetingDetailRequest &req,
                                                 qifeng_ca::MeetingDetailResponse &resp) {
        SLOG_DEBUG << "GetRecordingDetail - audio_id: " << req.audio_id();

        Status status = mMeetingDBService.GetRecordingDetail(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status MeetingController::EditRecording(const qifeng_ca::MeetingEditRequest &req, qifeng_ca::Empty &resp) {
        SLOG_DEBUG << "EditRecording - audio_id: " << req.audio_id();

        Status status = mMeetingDBService.EditRecording(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status MeetingController::DeleteRecording(const qifeng_ca::MeetingDeleteRequest &req, qifeng_ca::Empty &resp) {
        SLOG_DEBUG << "DeleteRecording";

        Status status = mMeetingDBService.DeleteRecording(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status MeetingController::GetTotalTime(const qifeng_ca::AccountRequst &req,
                                           qifeng_ca::MeetingTotalTimeResponse &resp) {
        SLOG_DEBUG << "GetTotalTime - accountId: " << req.account_id();

        Status status = mMeetingDBService.GetTotalTime(req.account_id(), &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status MeetingController::UpdateMarkers(const qifeng_ca::MarkerUpdateRequest &req, qifeng_ca::Empty &resp) {
        SLOG_DEBUG << "UpdateMarkers - audio_id: " << req.audio_id();

        Status status = mMeetingDBService.UpdateMarkers(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status MeetingController::AddRecording(const qifeng_ca::AddRecordingRequest &req,
                                           qifeng_ca::AddRecordingResponse &resp) {
        SLOG_DEBUG << "AddRecording - theme: " << req.theme();

        // 实时录音接口, is_rel_time必须为true
        Status status = mRecordingService.StartRealtimeRecording(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status MeetingController::UploadRecording(const qifeng_ca::AddRecordingRequest &req,
                                              qifeng_ca::AddRecordingResponse &resp) {
        SLOG_DEBUG << "UploadRecording - file_count: " << req.file_paths_size();

        // 离线上传录音接口, 前置处理已解析文件并保存到临时目录
        Status status = mRecordingService.UploadOfflineRecording(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status MeetingController::RecordStop(const qifeng_ca::RecordStopRequest &req, qifeng_ca::Empty &resp) {
        SLOG_DEBUG << "RecordStop - audio_id: " << req.audio_id();

        Status status = mRecordingService.RecordStop(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status MeetingController::PauseRecording(const qifeng_ca::RecordStopRequest &req, qifeng_ca::Empty &resp) {
        SLOG_DEBUG << "PauseRecording - audio_id: " << req.audio_id();

        Status status = mRecordingService.PauseRecording(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status MeetingController::ResumeRecording(const qifeng_ca::RecordStopRequest &req, qifeng_ca::Empty &resp) {
        SLOG_DEBUG << "ResumeRecording - audio_id: " << req.audio_id();

        Status status = mRecordingService.ResumeRecording(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status MeetingController::GetActiveRecording(const qifeng_ca::AccountRequst &req,
                                                 qifeng_ca::GetActiveRecordingResponse &resp) {
        SLOG_DEBUG << "GetActiveRecording - accountId: " << req.account_id();

        Status status = mMeetingDBService.GetActiveRecording(req.account_id(), &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status MeetingController::GetRecordInfo(const qifeng_ca::RecordInfoRequest &req,
                                            qifeng_ca::RecordInfoResponse &resp) {
        SLOG_DEBUG << "GetRecordInfo - audio_id: " << req.audio_id();

        Status status = mMeetingDBService.GetRecordInfo(req.account_id(), req.audio_id(), &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status MeetingController::DownloadAudio(const qifeng_ca::RecordInfoRequest &req,
                                            qifeng_ca::RecordDownloadResponse &resp) {
        SLOG_DEBUG << "DownloadAudio - audio_id: " << req.audio_id();

        Status status = mRecordingDownloadService.DownloadAudio(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status MeetingController::ResetTranscribeTask(const qifeng_ca::ResetTranscribeTaskRequest &req,
                                                  qifeng_ca::Empty &resp) {
        SLOG_DEBUG << "ResetTranscribeTask - audio_ids: " << req.audio_ids_size();

        Status status = mMeetingDBService.ResetTranscribeTask(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status MeetingController::SummaryDocumentPreview(const qifeng_ca::SummaryDocumentPreviewRequest &req,
                                                     qifeng_ca::SummaryDocumentPreviewResponse &resp) {
        SLOG_DEBUG << "SummaryDocumentPreview - audio_id: " << req.audio_id() << ", official: " << req.official();

        Status status = mRecordingDownloadService.SummaryDocumentPreview(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status MeetingController::RecordDownload(const qifeng_ca::RecordDownloadRequest &req,
                                             qifeng_ca::RecordDownloadResponse &resp) {
        SLOG_DEBUG << "RecordDownload - id: " << req.id() << ", contents: " << req.contents_size()
                   << ", official: " << req.official();

        Status status = mRecordingDownloadService.RecordDownload(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status MeetingController::GetSummaryKinds(const qifeng_ca::Empty &req, qifeng_ca::GetSummaryKindResponse &resp) {
        (void)req;
        SLOG_DEBUG << "GetSummaryKinds";

        Status status = mMeetingDBService.GetSummaryKinds(&resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status MeetingController::SaveTransformList(const qifeng_ca::SaveTransformRequest &req,
                                                qifeng_ca::SaveTransformResponse &resp) {
        SLOG_DEBUG << "SaveTransformList - audio_id: " << req.audio_id() << ", words: " << req.words_size();

        Status status = mMeetingDBService.SaveTransformList(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status MeetingController::SaveRecordingSummary(const qifeng_ca::SaveSummaryRequest &req, qifeng_ca::Empty &resp) {
        (void)resp;
        SLOG_DEBUG << "SaveRecordingSummary - audio_id: " << req.audio_id()
                   << ", summary_len: " << req.summary().size();

        Status status = mMeetingDBService.SaveRecordingSummary(req);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status MeetingController::RefreshSummary(const qifeng_ca::RefreshSummaryRequest &req,
                                             qifeng_ca::RefreshSummaryResponse &resp) {
        SLOG_DEBUG << "RefreshSummary - audio_id: " << req.audio_id() << ", kind: " << req.kind()
                   << ", topics: " << req.topics_size() << ", templateLen: " << req.template_text().size();

        Status status = mRefreshSummaryService.RefreshSummary(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    // ---------------------- 测试相关接口 START -------------------------
    Status MeetingController::GetAudioTransSummary(const qifeng_ca::AudioTransSummaryRequest &req,
                                                   qifeng_ca::AudioTransSummaryResponse &resp) {
        SLOG_DEBUG << "GetAudioTransSummary - audio_id: " << req.audio_id() << ", includeTrans: " << req.include_trans()
                   << ", includeSummary: " << req.include_summary();

        Status status = mRecordingService.GetAudioTransSummary(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status MeetingController::UploadRealtimeLikeRecording(const qifeng_ca::AddRecordingRequest &req,
                                                          qifeng_ca::AddRecordingResponse &resp) {
        SLOG_DEBUG << "UploadRealtimeLikeRecording - file_count: " << req.file_paths_size();

        // 文件模拟实时转写: 前置处理已解析文件并保存到临时目录, 转写采用实时provider(2s推送)
        Status status = mRecordingService.UploadRealtimeLikeRecording(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }
    // ---------------------- 测试相关接口 END -------------------------

}  // namespace qifeng_ca

QIFENG_CA_HTTP_METHOD_REGISTRY(qifeng_ca::MeetingController);
