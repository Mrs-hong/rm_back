//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_DAO_SPEAKER_DAO_H
#define QIFENG_CA_INCLUDE_DAO_SPEAKER_DAO_H

#include <cstdint>
#include <string>
#include <vector>

#include "dao/bms_base_dao.h"
#include "dao/models/bms_speaker.h"

namespace qifeng_ca {

    struct SpeakerSearchFilter {
        uint64_t mAccountId = 0;
        std::string mKeyword;
        int32_t mCurrent = 1;
        int32_t mPageSize = 20;
    };

    struct SpeakerSearchResult {
        int32_t mTotal = 0;
        std::vector<models::Speaker> mRecords;
    };

    class SpeakerDao : public BmsBaseDao {
    public:
        SpeakerDao() = default;
        ~SpeakerDao() override = default;

        SpeakerDao(const SpeakerDao &) = delete;
        SpeakerDao &operator=(const SpeakerDao &) = delete;
        SpeakerDao(SpeakerDao &&) = delete;
        SpeakerDao &operator=(SpeakerDao &&) = delete;

        models::Speaker GetById(uint64_t accountId, uint64_t id);

        models::Speaker GetByNumber(uint64_t accountId, const std::string &number);

        models::Speaker GetBySpeakerName(uint64_t accountId, const std::string &speakerName);

        models::Speaker GetBySpeakerId(uint64_t accountId, const std::string &speakerId);

        // 查询指定账户下所有声纹(用于缓存加载)
        std::vector<models::Speaker> ListByAccountId(uint64_t accountId);

        bool Insert(const models::Speaker &speaker);

        bool Update(uint64_t accountId, const models::Speaker &speaker);

        bool DeleteById(uint64_t accountId, uint64_t id);

        SpeakerSearchResult Search(const SpeakerSearchFilter &filter);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_SPEAKER_DAO_H
