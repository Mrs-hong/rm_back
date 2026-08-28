//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_NOTE_NOTE_SERVICE_H
#define QIFENG_CA_INCLUDE_CORE_NOTE_NOTE_SERVICE_H

#include "qifeng_ca/common.pb.h"
#include "qifeng_ca/note.pb.h"

#include "common/status.h"
#include "dao/audio_dao.h"
#include "dao/note_dao.h"

namespace qifeng_ca {

    class NoteService {
    public:
        NoteService() = default;
        ~NoteService() = default;

        NoteService(const NoteService &) = delete;
        NoteService &operator=(const NoteService &) = delete;
        NoteService(NoteService &&) noexcept = delete;
        NoteService &operator=(NoteService &&) = delete;

        Status SaveRecordingNote(const SaveRecordingNoteRequest &req, Empty* resp);

        Status GetRecordingNote(const GetRecordingNoteRequest &req, GetRecordingNoteResponse* resp);

    private:
        NoteDao mNoteDao;
        AudioDao mAudioDao;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_NOTE_NOTE_SERVICE_H
