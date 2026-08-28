//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_MEETING_RECORDING_DOWNLOAD_SERVICE_H
#define QIFENG_CA_INCLUDE_CORE_MEETING_RECORDING_DOWNLOAD_SERVICE_H

#include "qifeng_ca/meeting.pb.h"

#include "common/status.h"

namespace qifeng_ca {

    class RecordingDownloadService {
    public:
        RecordingDownloadService() = default;
        ~RecordingDownloadService() = default;

        RecordingDownloadService(const RecordingDownloadService &) = delete;
        RecordingDownloadService &operator=(const RecordingDownloadService &) = delete;
        RecordingDownloadService(RecordingDownloadService &&) noexcept = delete;
        RecordingDownloadService &operator=(RecordingDownloadService &&) = delete;

        Status SummaryDocumentPreview(const SummaryDocumentPreviewRequest &req, SummaryDocumentPreviewResponse* resp);

        Status DownloadAudio(const RecordInfoRequest &req, RecordDownloadResponse* resp);

        // 下载会议内容: 按contents列表生成docx/audio文件, 打包成tar返回路径
        Status RecordDownload(const RecordDownloadRequest &req, RecordDownloadResponse* resp);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_MEETING_RECORDING_DOWNLOAD_SERVICE_H
