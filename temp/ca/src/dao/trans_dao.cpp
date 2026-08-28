//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <cstdint>
#include <string>

#include "qifeng_framework/common/logger.h"

#include "dao/models/bms_trans.h"
#include "dao/trans_dao.h"

namespace qifeng_ca {

    constexpr std::string_view TransSelectSQL =
        "SELECT id, account_id, audio_id, start_time, end_time, speaker, speaker_name, "
        "content, is_orig, is_discard, seg_flag FROM trans";

    static void MapRowToTrans(const soci::row &row, models::Trans &trans) {
        trans.mId = row.get<uint64_t>(0);
        trans.mAccountId = row.get<uint64_t>(1);
        trans.mAudioId = row.get<std::string>(2);
        trans.mStartTime = row.get<int32_t>(3);
        trans.mEndTime = row.get<int32_t>(4);
        trans.mSpeaker = row.get<int32_t>(5);

        soci::indicator ind = row.get_indicator(6);
        if (ind == soci::i_ok) {
            trans.mSpeakerName = row.get<std::string>(6);
        }

        ind = row.get_indicator(7);
        if (ind == soci::i_ok) {
            trans.mContent = row.get<std::string>(7);
        }

        trans.mIsOrig = row.get<int>(8);
        trans.mIsDiscard = row.get<int>(9);
        trans.mSegFlag = row.get<int>(10);
    }

    std::vector<models::Trans> TransDao::GetByAudioId(uint64_t accountId, const std::string &audioId) {
        std::vector<models::Trans> result;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs =
                (session.prepare << std::string(TransSelectSQL) +
                                        " WHERE account_id = :account_id AND audio_id = :audio_id "
                                        "AND is_discard = 0 ORDER BY start_time ASC",
                 soci::use(accountId, "account_id"), soci::use(audioId, "audio_id"));
            for (const soci::row &row : rs) {
                models::Trans trans;
                MapRowToTrans(row, trans);
                result.push_back(std::move(trans));
            }
        } catch (const std::exception &e) {
            SLOG_ERROR << "TransDao::GetByAudioId failed: " << e.what();
        }
        return result;
    }

    bool TransDao::Insert(models::Trans &trans) {
        try {
            soci::session session = GetSession();
            session << "INSERT INTO trans (account_id, audio_id, start_time, end_time, "
                       "speaker, speaker_name, content, is_orig, is_discard, seg_flag) "
                       "VALUES (:account_id, :audio_id, :start_time, :end_time, "
                       ":speaker, :speaker_name, :content, :is_orig, :is_discard, :seg_flag)",
                soci::use(trans.mAccountId, "account_id"), soci::use(trans.mAudioId, "audio_id"),
                soci::use(trans.mStartTime, "start_time"), soci::use(trans.mEndTime, "end_time"),
                soci::use(trans.mSpeaker, "speaker"), soci::use(trans.mSpeakerName, "speaker_name"),
                soci::use(trans.mContent, "content"), soci::use(trans.mIsOrig, "is_orig"),
                soci::use(trans.mIsDiscard, "is_discard"), soci::use(trans.mSegFlag, "seg_flag");

            soci::indicator ind = soci::i_ok;
            session << "SELECT LAST_INSERT_ID()", soci::into(trans.mId, ind);
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "TransDao::Insert failed: " << e.what();
            return false;
        }
    }

    bool TransDao::BatchInsert(const std::vector<models::Trans> &transList) {
        if (transList.empty()) {
            return true;
        }
        try {
            soci::session session = GetSession();
            for (const auto &trans : transList) {
                session << "INSERT INTO trans (account_id, audio_id, start_time, end_time, "
                           "speaker, speaker_name, content, is_orig, is_discard, seg_flag) "
                           "VALUES (:account_id, :audio_id, :start_time, :end_time, "
                           ":speaker, :speaker_name, :content, :is_orig, :is_discard, :seg_flag)",
                    soci::use(trans.mAccountId, "account_id"), soci::use(trans.mAudioId, "audio_id"),
                    soci::use(trans.mStartTime, "start_time"), soci::use(trans.mEndTime, "end_time"),
                    soci::use(trans.mSpeaker, "speaker"), soci::use(trans.mSpeakerName, "speaker_name"),
                    soci::use(trans.mContent, "content"), soci::use(trans.mIsOrig, "is_orig"),
                    soci::use(trans.mIsDiscard, "is_discard"), soci::use(trans.mSegFlag, "seg_flag");
            }
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "TransDao::BatchInsert failed: " << e.what();
            return false;
        }
    }

    // 更新起始、结束时间、段标志、文本内容、说话人名称
    bool TransDao::UpdateContent(const models::Trans &trans) {
        try {
            soci::session session = GetSession();
            session << "UPDATE trans SET start_time = :start_time, end_time = :end_time, "
                       "seg_flag = :seg_flag, content = :content, speaker_name = :speaker_name "
                       "WHERE account_id = :account_id AND id = :id",
                soci::use(trans.mStartTime, "start_time"), soci::use(trans.mEndTime, "end_time"),
                soci::use(trans.mSegFlag, "seg_flag"), soci::use(trans.mContent, "content"),
                soci::use(trans.mSpeakerName, "speaker_name"), soci::use(trans.mAccountId, "account_id"),
                soci::use(trans.mId, "id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "TransDao::UpdateContent with trans failed: " << e.what();
            return false;
        }
    }

    bool TransDao::UpdateContent(uint64_t accountId, uint64_t transId, const std::string &content) {
        try {
            soci::session session = GetSession();
            session << "UPDATE trans SET content = :content "
                       "WHERE account_id = :account_id AND id = :id",
                soci::use(content, "content"), soci::use(accountId, "account_id"), soci::use(transId, "id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "TransDao::UpdateContent failed: " << e.what();
            return false;
        }
    }

    bool TransDao::DiscardById(uint64_t accountId, uint64_t transId) {
        try {
            soci::session session = GetSession();
            session << "UPDATE trans SET is_discard = 1 WHERE account_id = :account_id AND id = :id",
                soci::use(accountId, "account_id"), soci::use(transId, "id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "TransDao::DiscardById failed: " << e.what();
            return false;
        }
    }

    bool TransDao::DeleteById(uint64_t accountId, uint64_t transId) {
        try {
            soci::session session = GetSession();
            session << "DELETE FROM trans WHERE account_id = :account_id AND id = :id",
                soci::use(accountId, "account_id"), soci::use(transId, "id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "TransDao::DeleteById failed: " << e.what();
            return false;
        }
    }

    bool TransDao::DeleteByIds(uint64_t accountId, const std::vector<uint64_t> &ids) {
        if (ids.empty()) {
            return true;
        }
        try {
            soci::session session = GetSession();
            for (uint64_t id : ids) {
                session << "DELETE FROM trans WHERE account_id = :account_id AND id = :id",
                    soci::use(accountId, "account_id"), soci::use(id, "id");
            }
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "TransDao::DeleteByIds failed: " << e.what();
            return false;
        }
    }

    bool TransDao::DeleteByAudioId(uint64_t accountId, const std::string &audioId) {
        try {
            soci::session session = GetSession();
            session << "DELETE FROM trans WHERE account_id = :account_id AND audio_id = :audio_id",
                soci::use(accountId, "account_id"), soci::use(audioId, "audio_id");
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "TransDao::DeleteByAudioId failed: " << e.what();
            return false;
        }
    }

}  // namespace qifeng_ca
