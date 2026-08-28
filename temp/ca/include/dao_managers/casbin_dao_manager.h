/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_CA_INCLUDE_DAO_CASBIN_DAO_MANAGER_H
#define QIFENG_CA_INCLUDE_DAO_CASBIN_DAO_MANAGER_H

#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include "casbin/casbin.h"

#include "dao/casbin_dao.h"

namespace qifeng_ca {

    class CasbinDaoManager {
    public:
        static CasbinDaoManager &GetInstance();

        ~CasbinDaoManager() = default;

        CasbinDaoManager(const CasbinDaoManager &) = delete;
        CasbinDaoManager &operator=(const CasbinDaoManager &) = delete;
        CasbinDaoManager(CasbinDaoManager &&) = delete;
        CasbinDaoManager &operator=(CasbinDaoManager &&) = delete;

        void Initialize(const std::string &modelPath);

        bool Enforce(const std::string &subject, const std::string &urlPath, const std::string &action = "write");

        void ReloadPolicy();

        std::vector<CasbinRule> GetAllRules();

        bool AddGroupingRule(const std::string &v0, const std::string &v1);

        bool RemoveGroupingRule(const std::string &v0, const std::string &v1);

        bool AddPolicyRule(const std::string &sub, const std::string &obj, const std::string &act);

        bool AddUserToGroup(const std::string &account, uint64_t groupId);

        bool RemoveUserFromGroup(const std::string &account, uint64_t groupId);

        void InvalidateCache();

    private:
        CasbinDaoManager();

        void RefreshCache();

        void LoadPolicyIntoEnforcer();

        std::shared_ptr<CasbinDao> mDao;
        std::shared_ptr<casbin::Enforcer> mEnforcer;
        std::vector<CasbinRule> mCachedRules;
        std::mutex mMutex;
        // std::shared_mutex mRwMutex;
        std::string mModelPath;
        bool mCacheValid = false;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_CASBIN_DAO_MANAGER_H
