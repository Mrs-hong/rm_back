//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "qifeng_framework/common/logger.h"
#include "utf8/checked.h"

#include "common/common.h"
#include "common/config/note_config.h"
#include "core/note/note_service.h"
#include "dao/models/bms_audio.h"
#include "dao/models/bms_note.h"

namespace qifeng_ca {

    Status NoteService::SaveRecordingNote(const SaveRecordingNoteRequest &req, Empty* resp) {
        (void)resp;
        if (req.audio_id().empty()) {
            return Status {-1, "录音audio_id不能为空"};
        }
        if (req.content().empty()) {
            return Status {-1, "笔记内容不能为空"};
        }
        if (static_cast<size_t>(utf8::distance(req.content().begin(), req.content().end())) >
            static_cast<size_t>(NoteConfig::GetInstance().GetMaxNoteContentLength())) {
            SLOG_ERROR << "SaveRecordingNote: content length exceeds max limit, data: "
                       << static_cast<size_t>(utf8::distance(req.content().begin(), req.content().end()))
                       << "max: " << NoteConfig::GetInstance().GetMaxNoteContentLength();
            return Status {-1, "笔记内容过长"};
        }

        // 全局查询音频并校验访问权限
        models::User user = UserDaoManager::GetInstance().GetByAccountId(req.account_id());
        if (user.mAccountId == 0) {
            return Status {-1, "用户不存在"};
        }
        models::Audio audio = mAudioDao.GetByAudioIdGlobal(req.audio_id());
        if (audio.mId == 0) {
            return Status {-1, "录音不存在"};
        }
        if (!IsAudioAccessible(req.account_id(), user.mGroupId, audio.mAccountId)) {
            return Status {-1, "无权访问该录音"};
        }

        // 使用音频所有者的account_id保存笔记, 确保所有有权限的用户共享同一份笔记
        models::Note note;
        note.mAccountId = audio.mAccountId;
        note.mAudioId = req.audio_id();
        note.mContent = req.content();
        note.mTimestamp = static_cast<int64_t>(GetTimeMs());

        if (!mNoteDao.Upsert(note)) {
            return Status {-1, "保存笔记失败"};
        }
        return Status {};
    }

    static void FillNoteResponse(const models::Note &note, GetRecordingNoteResponse* resp) {
        resp->set_id(note.mId);
        resp->set_account_id(note.mAccountId);
        resp->set_audio_id(note.mAudioId);
        if (!note.mContent.empty()) {
            resp->set_content(note.mContent);
        }
        if (note.mTimestamp != 0) {
            resp->set_timestamp(note.mTimestamp);
        }
    }

    Status NoteService::GetRecordingNote(const GetRecordingNoteRequest &req, GetRecordingNoteResponse* resp) {
        if (req.audio_id().empty()) {
            return Status {-1, "请提供录音audio_id"};
        }

        // 全局查询音频并校验访问权限
        models::User user = UserDaoManager::GetInstance().GetByAccountId(req.account_id());
        if (user.mAccountId == 0) {
            return Status {-1, "用户不存在"};
        }
        models::Audio audio = mAudioDao.GetByAudioIdGlobal(req.audio_id());
        if (audio.mId == 0) {
            return Status {-1, "录音不存在"};
        }
        if (!IsAudioAccessible(req.account_id(), user.mGroupId, audio.mAccountId)) {
            return Status {-1, "无权访问该录音"};
        }

        // 使用音频所有者的account_id查询笔记, 确保所有有权限的用户看到同一份笔记
        models::Note note = mNoteDao.GetByAudioId(audio.mAccountId, req.audio_id());
        if (note.mAudioId.empty()) {
            return Status {-1, "笔记不存在"};
        }

        FillNoteResponse(note, resp);
        return Status {};
    }

}  // namespace qifeng_ca
