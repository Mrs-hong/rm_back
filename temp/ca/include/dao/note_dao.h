//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_DAO_NOTE_DAO_H
#define QIFENG_CA_INCLUDE_DAO_NOTE_DAO_H

#include <cstdint>
#include <string>

#include "dao/bms_base_dao.h"
#include "dao/models/bms_note.h"

namespace qifeng_ca {

    class NoteDao : public BmsBaseDao {
    public:
        NoteDao() = default;
        ~NoteDao() override = default;

        NoteDao(const NoteDao &) = delete;
        NoteDao &operator=(const NoteDao &) = delete;
        NoteDao(NoteDao &&) = delete;
        NoteDao &operator=(NoteDao &&) = delete;

        models::Note GetById(uint64_t id);

        models::Note GetByAudioId(uint64_t accountId, const std::string &audioId);

        bool Insert(models::Note &note);

        bool DeleteByAudioId(uint64_t accountId, const std::string &audioId);

        bool Upsert(const models::Note &note);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_NOTE_DAO_H
