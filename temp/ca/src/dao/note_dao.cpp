//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "qifeng_framework/common/logger.h"

#include "dao/models/bms_note.h"
#include "dao/note_dao.h"

namespace qifeng_ca {

    constexpr std::string_view NoteSelectSQL = "SELECT id, account_id, audio_id, content, timestamp FROM note";

    static void MapRowToNote(const soci::row &row, models::Note &note) {
        note.mId = row.get<uint64_t>(0);
        note.mAccountId = row.get<uint64_t>(1);
        note.mAudioId = row.get<std::string>(2);
        soci::indicator ind = row.get_indicator(3);
        if (ind == soci::i_ok) {
            note.mContent = row.get<std::string>(3);
        }
        note.mTimestamp = row.get<int64_t>(4);
    }

    models::Note NoteDao::GetById(uint64_t id) {
        models::Note note;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs =
                (session.prepare << std::string(NoteSelectSQL) << " WHERE id = :id", soci::use(id, "id"));
            for (const soci::row &row : rs) {
                MapRowToNote(row, note);
                break;
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "NoteDao::GetById failed: " << e.what();
        }
        return note;
    }

    models::Note NoteDao::GetByAudioId(uint64_t accountId, const std::string &audioId) {
        models::Note note;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs = (session.prepare << std::string(NoteSelectSQL)
                                                          << " WHERE account_id = :account_id AND audio_id = :audio_id",
                                          soci::use(accountId, "account_id"), soci::use(audioId, "audio_id"));
            for (const soci::row &row : rs) {
                MapRowToNote(row, note);
                break;
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "NoteDao::GetByAudioId failed: " << e.what();
        }
        return note;
    }

    bool NoteDao::Insert(models::Note &note) {
        try {
            soci::session session = GetSession();
            session << "INSERT INTO note (account_id, audio_id, content, timestamp) "
                       "VALUES (:account_id, :audio_id, :content, :timestamp)",
                soci::use(note.mAccountId, "account_id"), soci::use(note.mAudioId, "audio_id"),
                soci::use(note.mContent, "content"), soci::use(note.mTimestamp, "timestamp");

            soci::indicator ind = soci::i_ok;
            session << "SELECT LAST_INSERT_ID()", soci::into(note.mId, ind);
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "NoteDao::Insert failed: " << e.what();
            return false;
        }
    }

    bool NoteDao::DeleteByAudioId(uint64_t accountId, const std::string &audioId) {
        try {
            soci::session session = GetSession();
            session << "DELETE FROM note WHERE account_id = :account_id AND audio_id = :audio_id",
                soci::use(accountId, "account_id"), soci::use(audioId, "audio_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "NoteDao::DeleteByAudioId failed: " << e.what();
            return false;
        }
    }

    bool NoteDao::Upsert(const models::Note &note) {
        try {
            soci::session session = GetSession();
            session << "INSERT INTO note (account_id, audio_id, content, timestamp) "
                       "VALUES (:account_id, :audio_id, :content, :timestamp) "
                       "ON DUPLICATE KEY UPDATE content = :content, timestamp = :timestamp",
                soci::use(note.mAccountId, "account_id"), soci::use(note.mAudioId, "audio_id"),
                soci::use(note.mContent, "content"), soci::use(note.mTimestamp, "timestamp");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "NoteDao::Upsert failed: " << e.what();
            return false;
        }
    }

}  // namespace qifeng_ca
