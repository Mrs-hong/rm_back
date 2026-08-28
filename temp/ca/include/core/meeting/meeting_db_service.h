//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_MEETING_MEETING_DB_SERVICE_H
#define QIFENG_CA_INCLUDE_CORE_MEETING_MEETING_DB_SERVICE_H

#include "qifeng_ca/common.pb.h"
#include "qifeng_ca/meeting.pb.h"

#include "common/status.h"
#include "dao/audio_dao.h"

namespace qifeng_ca {

    class MeetingDBService {
    public:
        MeetingDBService() = default;
        ~MeetingDBService() = default;

        MeetingDBService(const MeetingDBService &) = delete;
        MeetingDBService &operator=(const MeetingDBService &) = delete;
        MeetingDBService(MeetingDBService &&) noexcept = delete;
        MeetingDBService &operator=(MeetingDBService &&) = delete;

        Status GetRecordingList(const MeetingSearchRequest &req, MeetingSearchResponse* resp);

        Status GetRecordingDetail(const MeetingDetailRequest &req, MeetingDetailResponse* resp);

        Status EditRecording(const MeetingEditRequest &req, Empty* resp);

        Status DeleteRecording(const MeetingDeleteRequest &req, Empty* resp);

        // 级联删除单个录音: 文件 + DB关联数据
        static Status DeleteAudioCascade(uint64_t accountId, const std::string &audioId);

        Status GetTotalTime(uint64_t accountId, MeetingTotalTimeResponse* resp);

        Status UpdateMarkers(const MarkerUpdateRequest &req, Empty* resp);

        Status GetActiveRecording(uint64_t accountId, GetActiveRecordingResponse* resp);

        Status GetRecordInfo(uint64_t accountId, const std::string &audioId, RecordInfoResponse* resp);

        Status ResetTranscribeTask(const ResetTranscribeTaskRequest &req, Empty* resp);

        Status GetSummaryKinds(GetSummaryKindResponse* resp);

        // 保存转写数据(含校验: 空数据/speaker长度/content长度/时间合法性/时间顺序)
        Status SaveTransformList(const SaveTransformRequest &req, SaveTransformResponse* resp);

        // 保存会议纪要
        Status SaveRecordingSummary(const SaveSummaryRequest &req);

    private:
        void FillSearchResponse(const AudioSearchResult &result, MeetingSearchResponse* resp);

        const models::Audio* FindActiveAudio(const std::vector<models::Audio> &records);

        void FillActiveAudioResponse(const models::Audio &audio, GetActiveRecordingResponse* resp);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_MEETING_MEETING_DB_SERVICE_H
