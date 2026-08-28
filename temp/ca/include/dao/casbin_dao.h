/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_CA_INCLUDE_DAO_CASBIN_DAO_H
#define QIFENG_CA_INCLUDE_DAO_CASBIN_DAO_H

#include <cstdint>
#include <string>
#include <vector>

#include "dao/bms_base_dao.h"

namespace qifeng_ca {

    struct CasbinRule {
        uint64_t mId = 0;
        std::string mPtype;
        std::string mV0;
        std::string mV1;
        std::string mV2;
        std::string mV3;
        std::string mV4;
        std::string mV5;
    };

    class CasbinDao : public BmsBaseDao {
    public:
        CasbinDao() = default;
        ~CasbinDao() override = default;

        CasbinDao(const CasbinDao &) = delete;
        CasbinDao &operator=(const CasbinDao &) = delete;
        CasbinDao(CasbinDao &&) = delete;
        CasbinDao &operator=(CasbinDao &&) = delete;

        std::vector<CasbinRule> LoadAllRules();

        bool InsertRule(const CasbinRule &rule);

        bool DeleteRule(const std::string &ptype, const std::string &v0, const std::string &v1);

        bool DeleteRuleById(uint64_t id);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_CASBIN_DAO_H
