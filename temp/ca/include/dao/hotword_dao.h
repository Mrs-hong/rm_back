/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_CA_INCLUDE_DAO_HOTWORD_DAO_H
#define QIFENG_CA_INCLUDE_DAO_HOTWORD_DAO_H

#include <cstdint>
#include <string>
#include <vector>

#include "dao/bms_base_dao.h"
#include "dao/models/bms_hotword.h"

namespace qifeng_ca {

    struct HotwordSearchFilter {
        uint64_t mAccountId = 0;
        std::string mKeyword;
        int32_t mStatus = 0;
        int64_t mStartTime = 0;
        int64_t mEndTime = 0;
        int32_t mCurrent = 1;
        int32_t mPageSize = 20;
    };

    struct HotwordSearchResult {
        int32_t mTotal = 0;
        std::vector<models::HotWord> mRecords;
    };

    class HotwordDao : public BmsBaseDao {
    public:
        HotwordDao() = default;
        ~HotwordDao() override = default;

        HotwordDao(const HotwordDao &) = delete;
        HotwordDao &operator=(const HotwordDao &) = delete;
        HotwordDao(HotwordDao &&) = delete;
        HotwordDao &operator=(HotwordDao &&) = delete;

        models::HotWord GetById(uint64_t accountId, uint64_t id);

        models::HotWord GetByWord(uint64_t accountId, const std::string &word);

        int64_t Count(uint64_t accountId);

        bool Insert(const models::HotWord &hotword);

        bool Update(uint64_t accountId, const models::HotWord &hotword);

        bool DeleteByIds(uint64_t accountId, const std::vector<uint64_t> &ids);

        HotwordSearchResult Search(const HotwordSearchFilter &filter);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_HOTWORD_DAO_H
