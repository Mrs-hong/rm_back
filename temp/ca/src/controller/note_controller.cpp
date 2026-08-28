//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "qifeng_framework/common/logger.h"

#include "controller/note_controller.h"

namespace qifeng_ca {

    Status NoteController::SaveRecordingNote(const qifeng_ca::SaveRecordingNoteRequest &req, qifeng_ca::Empty &resp) {
        SLOG_DEBUG << "SaveRecordingNote - audio_id: " << req.audio_id();

        Status status = mNoteService.SaveRecordingNote(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

    Status NoteController::GetRecordingNote(const qifeng_ca::GetRecordingNoteRequest &req,
                                            qifeng_ca::GetRecordingNoteResponse &resp) {
        SLOG_DEBUG << "GetRecordingNote - audio_id: " << req.audio_id();

        Status status = mNoteService.GetRecordingNote(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }
        return status;
    }

}  // namespace qifeng_ca

QIFENG_CA_HTTP_METHOD_REGISTRY(qifeng_ca::NoteController);
