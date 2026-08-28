/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "dao_managers/casbin_dao_manager.h"
#include "qifeng_framework/common/logger.h"

namespace qifeng_ca {

    CasbinDaoManager &CasbinDaoManager::GetInstance() {
        static CasbinDaoManager Instance;
        return Instance;
    }

    CasbinDaoManager::CasbinDaoManager() : mDao(std::make_shared<CasbinDao>()) {
        SLOG_INFO << "CasbinDaoManager initialized";
    }

    void CasbinDaoManager::Initialize(const std::string &modelPath) {
        mModelPath = modelPath;

        std::lock_guard<std::mutex> lock(mMutex);
        RefreshCache();
        LoadPolicyIntoEnforcer();
        FLOG_INFO("[CasbinDaoManager] Initialized, policy loaded from DB.");
    }

    void CasbinDaoManager::RefreshCache() {
        mCachedRules = mDao->LoadAllRules();
        mCacheValid = true;
        SLOG_INFO << "CasbinDaoManager cache refreshed, " << mCachedRules.size() << " rules";
    }

    void CasbinDaoManager::LoadPolicyIntoEnforcer() {
        auto model = casbin::Model::NewModelFromFile(mModelPath);
        mEnforcer = std::make_shared<casbin::Enforcer>(model);
        mEnforcer->EnableAutoSave(false);

        for (const auto &rule : mCachedRules) {
            std::vector<std::string> params;
            params.push_back(rule.mV0);
            params.push_back(rule.mV1);
            if (!rule.mV2.empty()) {
                params.push_back(rule.mV2);
            }

            if (rule.mPtype == "p") {
                mEnforcer->AddPolicy(params);
            } else if (rule.mPtype == "g") {
                mEnforcer->AddNamedGroupingPolicy("g", params);
            }
        }

        mEnforcer->BuildRoleLinks();

        FLOG_INFO("[CasbinDaoManager] Enforcer loaded " + std::to_string(mCachedRules.size()) + " rules.");
    }

    bool CasbinDaoManager::Enforce(const std::string &subject, const std::string &urlPath, const std::string &action) {
        std::lock_guard<std::mutex> lock(mMutex);
        // std::shared_lock<std::shared_mutex> lock(mRwMutex);
        if (!mEnforcer) {
            SLOG_ERROR << "[CasbinDaoManager] Enforcer not initialized";
            return false;
        }

        try {
            bool allowed = mEnforcer->Enforce({subject, urlPath, action});
            SLOG_INFO << "[CasbinDaoManager] enforce(" << subject << ", " << urlPath << ", " << action
                      << ") = " << (allowed ? "ALLOW" : "DENY");
            return allowed;
        } catch (const std::exception &e) {
            SLOG_ERROR << "[CasbinDaoManager] enforce error: " << e.what();
            return false;
        }
    }

    void CasbinDaoManager::ReloadPolicy() {
        std::lock_guard<std::mutex> lock(mMutex);
        // std::unique_lock<std::shared_mutex> lock(mRwMutex);
        RefreshCache();
        LoadPolicyIntoEnforcer();
        FLOG_INFO("[CasbinDaoManager] Policy reloaded.");
    }

    std::vector<CasbinRule> CasbinDaoManager::GetAllRules() {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mCacheValid) {
            return mCachedRules;
        }

        if (mCacheValid) {
            return mCachedRules;
        }
        RefreshCache();
        return mCachedRules;
    }

    void CasbinDaoManager::InvalidateCache() {
        std::lock_guard<std::mutex> lock(mMutex);
        mCacheValid = false;
        mCachedRules.clear();
        SLOG_INFO << "CasbinDaoManager cache invalidated";
    }

    bool CasbinDaoManager::AddGroupingRule(const std::string &v0, const std::string &v1) {
        CasbinRule rule;
        rule.mPtype = "g";
        rule.mV0 = v0;
        rule.mV1 = v1;
        bool ok = mDao->InsertRule(rule);
        if (ok) {
            std::lock_guard<std::mutex> lock(mMutex);
            RefreshCache();
            LoadPolicyIntoEnforcer();
        }
        return ok;
    }

    bool CasbinDaoManager::RemoveGroupingRule(const std::string &v0, const std::string &v1) {
        bool ok = mDao->DeleteRule("g", v0, v1);
        if (ok) {
            std::lock_guard<std::mutex> lock(mMutex);
            RefreshCache();
            LoadPolicyIntoEnforcer();
        }
        return ok;
    }

    bool CasbinDaoManager::AddPolicyRule(const std::string &sub, const std::string &obj, const std::string &act) {
        CasbinRule rule;
        rule.mPtype = "p";
        rule.mV0 = sub;
        rule.mV1 = obj;
        rule.mV2 = act;
        bool ok = mDao->InsertRule(rule);
        if (ok) {
            std::lock_guard<std::mutex> lock(mMutex);
            RefreshCache();
            LoadPolicyIntoEnforcer();
        }
        return ok;
    }

    bool CasbinDaoManager::AddUserToGroup(const std::string &account, uint64_t groupId) {
        std::string user = "user:" + account;
        std::string group = "group:" + std::to_string(groupId);
        return AddGroupingRule(user, group);
    }

    bool CasbinDaoManager::RemoveUserFromGroup(const std::string &account, uint64_t groupId) {
        std::string user = "user:" + account;
        std::string group = "group:" + std::to_string(groupId);
        return RemoveGroupingRule(user, group);
    }

}  // namespace qifeng_ca
