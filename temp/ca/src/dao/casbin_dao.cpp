/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "dao/casbin_dao.h"
#include "qifeng_framework/common/logger.h"

namespace qifeng_ca {

    static void MapRowToCasbinRule(const soci::row &row, CasbinRule &rule) {
        rule.mId = row.get<uint64_t>(0);
        rule.mPtype = row.get<std::string>(1);
        rule.mV0 = row.get<std::string>(2);
        rule.mV1 = row.get<std::string>(3);

        soci::indicator v2Ind = row.get_indicator(4);
        if (v2Ind == soci::i_ok) {
            rule.mV2 = row.get<std::string>(4);
        }
        soci::indicator v3Ind = row.get_indicator(5);
        if (v3Ind == soci::i_ok) {
            rule.mV3 = row.get<std::string>(5);
        }
        soci::indicator v4Ind = row.get_indicator(6);
        if (v4Ind == soci::i_ok) {
            rule.mV4 = row.get<std::string>(6);
        }
        soci::indicator v5Ind = row.get_indicator(7);
        if (v5Ind == soci::i_ok) {
            rule.mV5 = row.get<std::string>(7);
        }
    }

    std::vector<CasbinRule> CasbinDao::LoadAllRules() {
        std::vector<CasbinRule> rules;
        try {
            soci::session session = GetSession();
            soci::rowset<soci::row> rs =
                session.prepare << "SELECT id, ptype, v0, v1, v2, v3, v4, v5 FROM casbin_rule";
            for (const auto &row : rs) {
                CasbinRule rule;
                MapRowToCasbinRule(row, rule);
                rules.push_back(std::move(rule));
            }
            SLOG_INFO << "CasbinDao::LoadAllRules loaded " << rules.size() << " rules";
        } catch (const std::exception &e) {
            SLOG_ERROR << "CasbinDao::LoadAllRules failed: " << e.what();
        }
        return rules;
    }

    bool CasbinDao::InsertRule(const CasbinRule &rule) {
        try {
            soci::session session = GetSession();
            session << "INSERT INTO casbin_rule (ptype, v0, v1, v2, v3, v4, v5) "
                       "VALUES (:ptype, :v0, :v1, :v2, :v3, :v4, :v5)",
                soci::use(rule.mPtype, "ptype"), soci::use(rule.mV0, "v0"), soci::use(rule.mV1, "v1"),
                soci::use(rule.mV2, "v2"), soci::use(rule.mV3, "v3"), soci::use(rule.mV4, "v4"),
                soci::use(rule.mV5, "v5");
            SLOG_INFO << "CasbinDao::InsertRule: " << rule.mPtype << ", " << rule.mV0 << ", " << rule.mV1;
            return true;
        } catch (const std::exception &e) {
            SLOG_ERROR << "CasbinDao::InsertRule failed: " << e.what();
            return false;
        }
    }

    bool CasbinDao::DeleteRule(const std::string &ptype, const std::string &v0, const std::string &v1) {
        try {
            soci::session session = GetSession();
            soci::statement stmt =
                (session.prepare << "DELETE FROM casbin_rule WHERE ptype = :ptype AND v0 = :v0 AND v1 = :v1",
                 soci::use(ptype, "ptype"), soci::use(v0, "v0"), soci::use(v1, "v1"));
            stmt.execute(true);
            int affected = static_cast<int>(stmt.get_affected_rows());
            SLOG_INFO << "CasbinDao::DeleteRule: " << ptype << ", " << v0 << ", " << v1
                      << ", affected=" << affected;
            return affected > 0;
        } catch (const std::exception &e) {
            SLOG_ERROR << "CasbinDao::DeleteRule failed: " << e.what();
            return false;
        }
    }

    bool CasbinDao::DeleteRuleById(uint64_t id) {
        try {
            soci::session session = GetSession();
            soci::statement stmt = (session.prepare << "DELETE FROM casbin_rule WHERE id = :id", soci::use(id, "id"));
            stmt.execute(true);
            int affected = static_cast<int>(stmt.get_affected_rows());
            SLOG_INFO << "CasbinDao::DeleteRuleById: id=" << id << ", affected=" << affected;
            return affected > 0;
        } catch (const std::exception &e) {
            SLOG_ERROR << "CasbinDao::DeleteRuleById failed: " << e.what();
            return false;
        }
    }

}  // namespace qifeng_ca
