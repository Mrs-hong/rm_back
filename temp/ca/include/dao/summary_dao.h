//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_DAO_SUMMARY_DAO_H
#define QIFENG_CA_INCLUDE_DAO_SUMMARY_DAO_H

#include <cstdint>
#include <string>

#include "dao/bms_base_dao.h"
#include "dao/models/bms_summary.h"

namespace qifeng_ca {

    struct SummaryStats {
        int32_t mDone {0};
        int32_t mWaiting {0};
        int32_t mTotal {0};
    };

    class SummaryDao : public BmsBaseDao {
    public:
        SummaryDao() = default;
        ~SummaryDao() override = default;

        SummaryDao(const SummaryDao &) = delete;
        SummaryDao &operator=(const SummaryDao &) = delete;
        SummaryDao(SummaryDao &&) = delete;
        SummaryDao &operator=(SummaryDao &&) = delete;

        models::Summary GetByAudioId(uint64_t accountId, const std::string &audioId);

        bool Insert(const models::Summary &summary);

        bool UpdateContent(uint64_t accountId, const std::string &audioId, const std::string &content);

        bool DeleteByAudioId(uint64_t accountId, const std::string &audioId);

        SummaryStats CountStats();
        SummaryStats CountStatsByAccount(uint64_t accountId);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_SUMMARY_DAO_H
