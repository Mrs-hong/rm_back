//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <string>
#include <vector>

#include "qifeng_framework/common/logger.h"

#include "common/common.h"
#include "common/config/auth_config.h"
#include "common/status.h"
#include "core/meeting/meeting_db_service.h"
#include "core/system/reset_task.h"
#include "core/user/user_service.h"
#include "dao_managers/meeting_dao_manager.h"
#include "dao_managers/user_dao_manager.h"
#include "qifeng_ca/user.pb.h"

namespace qifeng_ca {

    namespace {

        // 获取需要删除的账户account_id列表(排除第一个group=1管理员与第一个group=3访客)
        std::vector<uint64_t> GetDeletableUserIds(uint64_t keepAdminId, uint64_t keepGuestId) {
            UserSearchFilter filter;
            filter.mPageSize = 10000;
            filter.mCurrent = 1;
            auto result = UserDaoManager::GetInstance().Search(filter);
            std::vector<uint64_t> ids;
            for (const auto &u : result.mRecords) {
                if (u.mAccountId == keepAdminId || u.mAccountId == keepGuestId) {
                    continue;
                }
                ids.push_back(u.mAccountId);
            }
            return ids;
        }

        // 通过UserService删除用户(级联清理音频记录/文件与指纹模板)
        Status DeleteUsersCascade(uint64_t operatorAccountId, const std::vector<uint64_t> &ids) {
            UserDelRequest req;
            req.set_account_id(operatorAccountId);
            for (uint64_t aid : ids) {
                req.add_target_account_ids(aid);
            }
            Empty resp;
            return UserService().DeleteUsers(req, &resp);
        }

        // 级联删除账户的音频数据(不删除账户本身)
        void CascadeDeleteAccountAudio(uint64_t accountId) {
            auto audioIds = MeetingDaoManager::GetInstance().GetAudioIdsByAccountId(accountId);
            for (const auto &audioId : audioIds) {
                auto st = MeetingDBService::DeleteAudioCascade(accountId, audioId);
                if (!st.IsSuccess()) {
                    SLOG_WARN << "Reset: cascade delete audio failed, accountId=" << accountId << " audioId=" << audioId
                              << " status=" << st.ToString();
                }
            }
        }

        // 重置默认管理员账号: 密码恢复默认, 清空除账号名/密码外的所有字段, user_name 改为 "系统管理员"
        Status ResetAdminAccount(uint64_t adminAccountId) {
            if (adminAccountId == 0) {
                SLOG_WARN << "Reset: admin account not found";
                return Status {-1, "管理员账号不存在"};
            }
            // 默认密码 hash 必须在 config.yaml 中配置(default_admin_password),
            // 与 database/sql/50_admin_init.sql 中的初始密码保持一致.
            // 不允许硬编码密码 hash 在源码中, 避免与部署环境不一致或随源码泄露.
            auto defaultPassword = AuthConfig::GetInstance().GetDefaultAdminPassword();
            if (defaultPassword.empty()) {
                SLOG_ERROR << "Reset: default admin password not configured (auth.default_admin_password)";
                return Status {-1, "默认管理员密码未配置"};
            }
            auto user = UserDaoManager::GetInstance().GetByAccountId(adminAccountId);
            if (user.mAccountId == 0) {
                return Status {-1, "管理员账号不存在"};
            }
            // 保留: mAccount(账号名), mGroupId(管理员组), mCreateTime
            // 重置: mPassword(默认密码), mUserName("系统管理员")
            // 清空: mEmail, mPhone, mFingerprintId, mComment, mAccessToken, mRefreshToken
            user.mUserName = "系统管理员";
            user.mEmail.clear();
            user.mPhone.clear();
            user.mFingerprintId = 0;
            user.mComment.clear();
            user.mAccessToken.clear();
            user.mRefreshToken.clear();
            user.mPassword = std::move(defaultPassword);
            if (!UserDaoManager::GetInstance().Update(user)) {
                return Status {-1, "重置管理员账号失败"};
            }
            SLOG_INFO << "Reset: admin account reset to default, accountId=" << adminAccountId;
            return Status {};
        }

    }  // namespace

    // 删除除首个管理员/访客外的所有账号, 重置默认管理员密码, 清理保留默认账户的音频与指纹数据
    // 所有删除操作的执行者均为默认管理员(第一个group_id=1账号)
    class DeleteUsersResetTask final : public ResetTask {
    public:
        std::string_view Name() const override { return "DeleteUsers"; }

        Status Execute(uint64_t operatorAccountId) override {
            (void)operatorAccountId;
            // 默认管理员 = 第一个group_id=1账号, 作为所有删除操作的执行者
            uint64_t adminId = GetDefaultAdminAccountId();
            if (adminId == 0) {
                return Status {-1, "默认管理员账号不存在"};
            }
            uint64_t guestId = GetGuestAccountId();

            Status st = DeleteAccountsExceptDefaults(adminId, guestId);
            if (!st.IsSuccess()) {
                return st;
            }
            st = ResetAdminAccount(adminId);
            if (!st.IsSuccess()) {
                return st;
            }
            return CleanupDefaultAccountsData(adminId, guestId);
        }

    private:
        // 删除除首个管理员与首个访客外的所有账号(操作者=默认管理员)
        Status DeleteAccountsExceptDefaults(uint64_t adminId, uint64_t guestId) {
            auto ids = GetDeletableUserIds(adminId, guestId);
            if (ids.empty()) {
                SLOG_INFO << "Reset: no accounts to delete";
                return Status {};
            }
            auto st = DeleteUsersCascade(adminId, ids);
            if (!st.IsSuccess()) {
                SLOG_WARN << "Reset: delete accounts failed, " << st.ToString();
            }
            return Status {};
        }

        // 清理保留的默认账户(首个管理员+首个访客)的音频与指纹数据
        Status CleanupDefaultAccountsData(uint64_t adminId, uint64_t guestId) {
            std::vector<uint64_t> ids;
            if (adminId != 0) {
                ids.push_back(adminId);
            }
            if (guestId != 0) {
                ids.push_back(guestId);
            }
            for (uint64_t aid : ids) {
                ResetFingerprints(aid, adminId);
                CascadeDeleteAccountAudio(aid);
            }
            SLOG_INFO << "Reset: cleaned audio data for " << ids.size() << " default accounts";
            return Status {};
        }

        // 重置指定账户的指纹(操作者=默认管理员)
        Status ResetFingerprints(uint64_t accountId, uint64_t operatorAccountId) {
            FingerprintDeleteRequest req;
            req.set_target_account_id(accountId);
            req.set_account_id(operatorAccountId);
            FingerprintOperateResponse resp;
            auto st = UserService().DeleteFingerprint(req, &resp);
            if (!st.IsSuccess()) {
                SLOG_WARN << "Reset: delete fingerprints failed, " << st.ToString();
            }
            return Status {};
        }
    };

    std::unique_ptr<ResetTask> CreateDeleteUsersResetTask() {
        return std::make_unique<DeleteUsersResetTask>();
    }

}  // namespace qifeng_ca
