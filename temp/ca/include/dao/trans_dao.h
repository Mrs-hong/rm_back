//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_DAO_TRANS_DAO_H
#define QIFENG_CA_INCLUDE_DAO_TRANS_DAO_H

#include <cstdint>
#include <string>
#include <vector>

#include "dao/bms_base_dao.h"
#include "dao/models/bms_trans.h"

namespace qifeng_ca {

    class TransDao : public BmsBaseDao {
    public:
        TransDao() = default;
        ~TransDao() override = default;

        TransDao(const TransDao &) = delete;
        TransDao &operator=(const TransDao &) = delete;
        TransDao(TransDao &&) = delete;
        TransDao &operator=(TransDao &&) = delete;

        std::vector<models::Trans> GetByAudioId(uint64_t accountId, const std::string &audioId);

        bool Insert(models::Trans &trans);

        bool BatchInsert(const std::vector<models::Trans> &transList);

        bool UpdateContent(uint64_t accountId, uint64_t transId, const std::string &content);

        // 更新起始、结束时间、段标志、文本内容
        bool UpdateContent(const models::Trans &trans);

        bool DiscardById(uint64_t accountId, uint64_t transId);

        bool DeleteById(uint64_t accountId, uint64_t transId);

        bool DeleteByIds(uint64_t accountId, const std::vector<uint64_t> &ids);

        bool DeleteByAudioId(uint64_t accountId, const std::string &audioId);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_TRANS_DAO_H
