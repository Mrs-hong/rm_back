//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CONTROLLER_NOTE_CONTROLLER_H
#define QIFENG_CA_INCLUDE_CONTROLLER_NOTE_CONTROLLER_H

#include "drogon/DrObject.h"
#include "qifeng_ca/common.pb.h"
#include "qifeng_ca/note.pb.h"

#include "common/http/http.h"
#include "common/status.h"
#include "core/note/note_service.h"

namespace qifeng_ca {

    class NoteController final : public drogon::DrObject<NoteController> {
    public:
        QIFENG_CA_METHOD_LIST_BEGIN(NoteController);

        QIFENG_CA_METHOD_PREREQ_ADD("保存录音笔记", SaveRecordingNote, BmsPreAccountIdReq<SaveRecordingNoteRequest>,
                                    "/web/note/saveRecordingNote", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD(ActionDesc("获取录音笔记", false), GetRecordingNote,
                                    BmsPreAccountIdReq<GetRecordingNoteRequest>, "/web/note/getRecordingNote",
                                    drogon::Post);

        QIFENG_CA_METHOD_LIST_END;

        Status SaveRecordingNote(const SaveRecordingNoteRequest &req, Empty &resp);

        Status GetRecordingNote(const GetRecordingNoteRequest &req, GetRecordingNoteResponse &resp);

    private:
        NoteService mNoteService;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CONTROLLER_NOTE_CONTROLLER_H
