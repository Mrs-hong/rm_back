//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <memory>
#include <regex>
#include <string>

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/security/password_hasher.h"
#include "qifeng_framework/common/utils/time.h"
#include "utf8/checked.h"

#include "common/common.h"
#include "common/status.h"
#include "core/meeting/meeting_db_service.h"
#include "core/user/fingerprint_enroller.h"
#include "core/user/user_service.h"
#include "dao/models/bms_user.h"
#include "dao_managers/meeting_dao_manager.h"
#include "internal/hal/fingerprint_bridge.h"
#include "internal/recording_manager.h"

namespace qifeng_ca {

    UserDaoManager &UserService::Dao() {
        return UserDaoManager::GetInstance();
    }

    static bool ValidateAccountFormat(const std::string &account) {
        try {
            if (account.empty() || account.length() > 16) {
                return false;
            }
            std::regex pattern(R"(^[a-zA-Z0-9!@#$%^&*()_+\-=\[\]{};':"\\|,.<>/?]{1,16}$)");
            return std::regex_match(account, pattern);
        } catch (const std::exception &e) {
            return false;
        }
    }

    static bool ValidateEmailFormat(const std::string &email) {
        try {
            if (email.empty()) {
                return true;
            }
            std::regex pattern(R"(^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9-]+\.[a-zA-Z]{2,}$)");
            return std::regex_match(email, pattern);
        } catch (const std::exception &e) {
            return false;
        }
    }

    static bool ValidatePhoneFormat(const std::string &phone) {
        try {
            if (phone.empty()) {
                return true;
            }
            std::regex pattern(R"(^1[3-9]\d{9}$)");
            return std::regex_match(phone, pattern);
        } catch (const std::exception &e) {
            return false;
        }
    }

    // 密码格式校验: 采用白名单方式, 仅允许字母、数字及安全特殊字符
    // 禁止中文、空格、制表符、反斜杠、单双引号、反引号等可能导致SQL注入的字符
    // 强制要求同时包含: 字母、数字、特殊符号
    static Status ValidatePasswordFormat(const std::string &password) {
        try {
            bool ret = !password.empty() && password.length() >= 8 && password.length() <= 20;
            if (!ret) {
                return {-1, "密码强度不足，必须包含8-20位字母、数字、特殊字符"};
            }
            // 白名单: a-z A-Z 0-9 !@#$%^&*()_+-=[]{}|;:,.<>/?~
            // 排除: \ ' " ` 空格 及任何非ASCII字符(含中文)
            std::regex pattern(R"(^[a-zA-Z0-9!@#$%^&*()_+\-=\[\]{}|;:,.<>/?~]+$)");
            ret = std::regex_match(password, pattern);
            if (!ret) {
                return Status {-1, "密码格式无效，仅支持字母、数字及安全特殊字符(不允许中文、空格、反斜杠、引号等)"};
            }

            // 必须同时包含字母、数字、特殊符号
            std::regex patternRegex(
                R"(^(?=.*[a-zA-Z])(?=.*[0-9])(?=.*[!@#$%^&*()_+\-=\[\]{}|;:,.<>/?~])[a-zA-Z0-9!@#$%^&*()_+\-=\[\]{}|;:,.<>/?~]+$)");
            ret = std::regex_match(password, patternRegex);
            if (!ret) {
                return Status {-1, "密码格式无效，必须同时包含字母、数字、特殊符号"};
            }
        } catch (const std::exception &e) {
            return Status {-1, "密码格式异常"};
        }
        return {};
    }

    static Status ValidateAddUserReq(const UserAddRequest &req) {
        if (!ValidateAccountFormat(req.account())) {
            return Status {-1, "账号格式无效，仅支持1-16位的字母数字特殊字符组合"};
        }
        auto status = ValidatePasswordFormat(req.password());
        if (!status.IsSuccess()) {
            return status;
        }
        if (req.user_name().empty() || utf8::distance(req.user_name().begin(), req.user_name().end()) > 16) {
            return Status {-1, "用户名长度必须在1-16位之间"};
        }
        if (!ValidateEmailFormat(req.email())) {
            return Status {-1, "邮箱格式不正确"};
        }
        if (!ValidatePhoneFormat(req.phone())) {
            return Status {-1, "手机号格式不正确"};
        }
        return Status {};
    }

    static models::User BuildNewUser(const UserAddRequest &req) {
        models::User newUser;
        newUser.mAccountId = GetUuid();
        newUser.mAccount = req.account();
        newUser.mPassword = common::security::PasswordHasher::HashPassword(req.password());
        newUser.mUserName = req.user_name();
        newUser.mGroupId = Authority::NORMAL_USER;
        if (req.has_email()) {
            newUser.mEmail = req.email();
        }
        if (req.has_phone()) {
            newUser.mPhone = req.phone();
        }
        if (req.has_comment()) {
            newUser.mComment = req.comment();
        }
        newUser.mCreateTime = static_cast<int64_t>(GetTimeMs());
        return newUser;
    }

    Status UserService::UserOperator(uint64_t accountId, uint64_t operatorAccountId) {
        Status status;
        if (accountId == operatorAccountId) {
            return status;
        }
        models::User existing = Dao().GetByAccountId(accountId);
        if (existing.mAccountId == 0) {
            return {-1, "用户不存在"};
        }
        models::User operatorExisting = Dao().GetByAccountId(operatorAccountId);
        if (operatorExisting.mAccountId == 0) {
            return {-1, "用户不存在"};
        }
        if (operatorExisting.mGroupId != Authority::ADMINISTRATOR) {
            return {-1, "无操作权限"};
        }
        return status;
    }

    Status UserService::Login(const UserLoginRequest &req, UserLoginResponse* resp) {
        if (req.account().empty()) {
            return Status {-1, "用户名为空"};
        }
        if (utf8::distance(req.account().begin(), req.account().end()) > 64) {
            return Status {-1, "用户名输入过长"};
        }
        if (req.password().empty()) {
            return Status {-1, "密码为空"};
        }

        models::User user = Dao().GetByAccount(req.account());
        if (user.mAccountId == 0 || !common::security::PasswordHasher::VerifyPassword(req.password(), user.mPassword)) {
            return Status {-1, "用户不存在或者密码错误"};
        }

        uint64_t accountId = user.mAccountId;
        std::string accessToken = GenerateAccessToken(accountId);
        std::string refreshToken = GenerateRefreshToken(accountId);

        auto exData = std::make_shared<ExDataInfo>();
        exData->mAccountId = accountId;
        Status status {};
        status.SetExData(exData);

        if (!Dao().UpdateTokens(accountId, accessToken, refreshToken)) {
            return status.SetResult(-1, "更新令牌失败");
        }

        resp->set_access_token(accessToken);
        resp->set_refresh_token(refreshToken);
        resp->set_account_id(accountId);
        return status;
    }

    Status UserService::GuestLogin(UserLoginResponse* resp) {
        models::User user = Dao().GetByAccountId(GetGuestAccountId());
        if (user.mAccountId == 0 || user.mGroupId != Authority::GUEST) {
            return Status {-1, "访客账户不存在"};
        }

        uint64_t accountId = user.mAccountId;
        std::string accessToken = GenerateAccessToken(accountId);
        std::string refreshToken = GenerateRefreshToken(accountId);

        auto exData = std::make_shared<ExDataInfo>();
        exData->mAccountId = accountId;
        Status status {};
        status.SetExData(exData);

        if (!Dao().UpdateTokens(accountId, accessToken, refreshToken)) {
            return status.SetResult(-1, "更新令牌失败");
        }

        resp->set_access_token(accessToken);
        resp->set_refresh_token(refreshToken);
        resp->set_account_id(accountId);
        return status;
    }

    Status UserService::RefreshToken(const UserRefreshRequest &req, UserRefreshResponse* resp) {
        if (req.refresh_token().empty()) {
            return Status {-1, "Refresh token is empty"};
        }

        uint64_t accountId = req.account_id();

        models::User user = Dao().GetByAccountIdAndRefreshToken(accountId, req.refresh_token());
        if (user.mAccountId == 0) {
            return Status {-1, "Refresh token expired or invalid"};
        }

        std::string accessToken = GenerateAccessToken(accountId);
        if (!Dao().UpdateTokens(accountId, accessToken, req.refresh_token())) {
            return Status {-1, "更新令牌失败"};
        }

        resp->set_access_token(accessToken);
        return Status {};
    }

    Status UserService::GetUserInfo(const AccountRequst &req, UserInfoResponse* resp) {
        models::User user = Dao().GetByAccountId(req.account_id());
        if (user.mAccountId == 0) {
            return Status {-1, "用户不存在"};
        }

        resp->set_account_id(user.mAccountId);
        resp->set_account(user.mAccount);
        if (!user.mUserName.empty()) {
            resp->set_user_name(user.mUserName);
        }
        if (!user.mEmail.empty()) {
            resp->set_email(user.mEmail);
        }
        if (!user.mComment.empty()) {
            resp->set_comment(user.mComment);
        }
        if (user.mGroupId != 0) {
            resp->set_type(std::to_string(user.mGroupId));
        }
        if (!user.mPhone.empty()) {
            resp->set_phone(user.mPhone);
        }

        return Status {};
    }

    Status UserService::Logout(const LogoutRequest &req, Empty* resp) {
        (void)resp;
        if (!req.flag() && !Dao().ClearTokens(req.account_id())) {
            return Status {-1, "登出失败"};
        }
        return Status {};
    }

    Status UserService::GetUserList(const UserSearchRequest &req, UserSearchListResponse* resp) {
        if (req.page_size() > 100) {
            return Status {-1, "page_size 参数异常"};
        }
        UserSearchFilter filter;
        filter.mCurrent = req.current();
        filter.mPageSize = req.page_size();
        if (req.has_keyword()) {
            filter.mKeyword = SanitizeKeyword(req.keyword());
        }

        UserSearchResult result = Dao().Search(filter);

        resp->set_total(result.mTotal);
        for (const auto &u : result.mRecords) {
            auto* item = resp->add_records();
            item->set_user_name(u.mUserName);
            item->set_account_id(u.mAccountId);
            item->set_account(u.mAccount);
            item->set_type(std::to_string(u.mGroupId));
            item->set_email(u.mEmail);
            item->set_phone(u.mPhone);
            item->set_comment(u.mComment);
            item->set_create_time(u.mCreateTime);
            // 返回是否已录入指纹(不返回指纹ID)
            item->set_has_fingerprint(u.mFingerprintId != 0);
        }

        return Status {};
    }

    Status UserService::AddUser(const UserAddRequest &req, Empty* resp) {
        (void)resp;

        Status status = ValidateAddUserReq(req);
        if (!status.IsSuccess()) {
            FLOG_ERROR(status.ToString());
            return status;
        }

        models::User existing = Dao().GetByAccount(req.account());
        if (existing.mAccountId != 0) {
            status = {-1, "账号已存在"};
            FLOG_ERROR(status.ToString());
            return status;
        }

        models::User newUser = BuildNewUser(req);
        if (!Dao().Insert(newUser)) {
            status = {-1, "添加用户失败"};
            FLOG_ERROR(status.ToString());
            return status;
        }

        return Status {};
    }

    Status UserService::UpdateUser(const UserUpdateRequest &req, Empty* resp) {
        Status status;
        (void)resp;

        if (req.user_name().empty() || utf8::distance(req.user_name().begin(), req.user_name().end()) > 16) {
            status = {-1, "用户名长度必须在1-16位之间"};
            FLOG_ERROR(status.ToString());
            return status;
        }

        if (!ValidateEmailFormat(req.email())) {
            status = {-1, "邮箱格式不正确"};
            FLOG_ERROR(status.ToString());
            return status;
        }

        if (!ValidatePhoneFormat(req.phone())) {
            status = {-1, "手机号格式不正确"};
            FLOG_ERROR(status.ToString());
            return status;
        }

        status = UserOperator(req.target_account_id(), req.account_id());
        if (status.GetCode() != 0) {
            return status;
        }

        models::User updateUser = Dao().GetByAccountId(req.target_account_id());
        updateUser.mUserName = req.user_name();
        if (req.has_email()) {
            updateUser.mEmail = req.email();
        }
        if (req.has_phone()) {
            updateUser.mPhone = req.phone();
        }
        if (req.has_comment()) {
            updateUser.mComment = req.comment();
        }

        if (!Dao().Update(updateUser)) {
            status = {-1, "更新用户信息失败"};
            FLOG_ERROR(status.ToString());
            return status;
        }

        return status;
    }

    Status UserService::UpdatePwd(const UserUpdatePwdRequest &req, Empty* resp) {
        Status status;
        (void)resp;
        if (req.old_pwd().empty() || req.new_pwd().empty()) {
            status = {-1, "密码不能为空"};
            FLOG_ERROR(status.ToString());
            return status;
        }
        status = ValidatePasswordFormat(req.new_pwd());
        if (!status.IsSuccess()) {
            return status;
        }

        models::User user = Dao().GetByAccountId(req.account_id());
        if (user.mAccountId == 0 || !common::security::PasswordHasher::VerifyPassword(req.old_pwd(), user.mPassword)) {
            status = {-1, "原密码不正确"};
            FLOG_ERROR(status.ToString());
            return status;
        }

        if (req.old_pwd() == req.new_pwd()) {
            status = {-1, "新密码不能与旧密码相同"};
            FLOG_ERROR(status.ToString());
            return status;
        }

        if (!Dao().UpdatePassword(req.account_id(), common::security::PasswordHasher::HashPassword(req.new_pwd()))) {
            status = {-1, "修改密码失败"};
            FLOG_ERROR(status.ToString());
            return status;
        }

        SLOG_INFO << "Password updated for account_id: " << req.account_id();
        return status;
    }

    Status UserService::UpdatePwdById(const UserUpdatePwdByIdRequest &req, Empty* resp) {
        (void)resp;

        if (req.new_pwd().empty()) {
            return Status {-1, "新密码不能为空"};
        }
        auto status = ValidatePasswordFormat(req.new_pwd());
        if (!status.IsSuccess()) {
            return status;
        }

        uint64_t targetAccountId = req.target_account_id();
        models::User targetUser = Dao().GetByAccountId(targetAccountId);
        if (targetUser.mAccountId == 0) {
            return Status {-1, "目标用户不存在"};
        }

        status = UserOperator(req.target_account_id(), req.account_id());
        if (status.GetCode() != 0) {
            return status;
        }

        if (!Dao().UpdatePassword(targetAccountId, common::security::PasswordHasher::HashPassword(req.new_pwd()))) {
            return Status {-1, "重置密码失败"};
        }

        return Status {};
    }

    // 级联删除用户的所有音频记录及文件
    static void CascadeDeleteUserAudio(uint64_t accountId) {
        std::vector<std::string> audioIds = MeetingDaoManager::GetInstance().GetAudioIdsByAccountId(accountId);
        for (const auto &audioId : audioIds) {
            Status st = MeetingDBService::DeleteAudioCascade(accountId, audioId);
            if (!st.IsSuccess()) {
                SLOG_WARN << "DeleteUsers: cascade delete audio failed, accountId=" << accountId
                          << " audioId=" << audioId << " status=" << st.ToString();
            }
        }
    }

    // 删除用户注册的指纹模板(若存在)
    static void TryDeleteUserFingerprint(uint64_t accountId) {
        models::User user = UserDaoManager::GetInstance().GetByAccountId(accountId);
        if (user.mFingerprintId == 0) {
            return;
        }
        auto fingerId = static_cast<uint16_t>(user.mFingerprintId);
        constexpr uint32_t kTimeoutMs = 10000;
        auto delRet = FingerprintBridge::GetInstance().Delete(fingerId, kTimeoutMs);
        if (delRet != FingerprintResult::OK && delRet != FingerprintResult::NotFound) {
            SLOG_WARN << "DeleteUsers: delete fingerprint failed, accountId=" << accountId << " fingerId=" << fingerId
                      << " ret=" << static_cast<int>(delRet);
        } else {
            SLOG_INFO << "DeleteUsers: deleted fingerprint, accountId=" << accountId << " fingerId=" << fingerId;
        }
    }

    Status UserService::DeleteUsers(const UserDelRequest &req, Empty* resp) {
        (void)resp;
        if (req.target_account_ids_size() == 0) {
            return Status {-1, "请选择要删除的用户"};
        }

        std::vector<uint64_t> accountIds;
        for (int i = 0; i < req.target_account_ids_size(); ++i) {
            accountIds.push_back(req.target_account_ids(i));
        }

        for (const auto &aid : accountIds) {
            if (aid == req.account_id()) {
                return Status {-1, "不能删除自己的账号"};
            }
            if (aid == GetGuestAccountId()) {
                return Status {-1, "访客账户不可删除"};
            }
        }

        models::User operatorExisting = Dao().GetByAccountId(req.account_id());
        if (operatorExisting.mGroupId != Authority::ADMINISTRATOR) {
            return {-1, "无操作权限"};
        }

        // 删除用户前, 级联删除音频记录及指纹模板
        for (const auto &aid : accountIds) {
            if (RecordingManager::GetInstance().GetActiveAccountId() == aid) {
                return Status {-1, "当前有正在进行的录音, 不能删除用户"};
            }
            CascadeDeleteUserAudio(aid);
            TryDeleteUserFingerprint(aid);
        }

        if (!Dao().DeleteByAccountIds(accountIds)) {
            return Status {-1, "删除用户失败"};
        }

        SLOG_INFO << "Users deleted by admin " << req.account_id();
        return Status {};
    }

    Status UserService::ResetAdminPassword(const std::string &adminAccount, const std::string &newPassword) {
        auto status = ValidatePasswordFormat(newPassword);
        if (!status.IsSuccess()) {
            return status;
        }

        models::User user = Dao().GetByAccount(adminAccount);
        if (user.mAccountId == 0) {
            return Status {-1, "超管账号不存在"};
        }

        if (user.mGroupId != Authority::ADMINISTRATOR) {
            return Status {-1, "账号不是超管账号"};
        }

        std::string hashedPwd = common::security::PasswordHasher::HashPassword(newPassword);
        bool ok = Dao().UpdatePassword(user.mAccountId, hashedPwd);
        if (!ok) {
            return Status {-1, "密码重置失败"};
        }

        SLOG_INFO << "Admin password reset success for account: " << adminAccount;
        return Status {};
    }

    Status UserService::AdminForgotPassword(AdminForgotPasswordResponse* resp) {
        SLOG_DEBUG << "AdminForgotPassword";

        std::string encryptionCode = Dao().GenDynamicCode();
        if (encryptionCode.empty()) {
            return Status {-1, "动态码生成失败"};
        }

        resp->set_encryption_code(encryptionCode);
        SLOG_INFO << "Admin forgot password request processed, dynamic code generated";
        return Status {};
    }

    Status UserService::AdminResetPassword(const AdminResetPasswordRequest &req) {
        SLOG_DEBUG << "AdminResetPassword for account: " << req.account();

        if (req.verify_code().empty() || req.dynamic().empty()) {
            return Status {-1, "动态码不能为空"};
        }

        Status status = Dao().VerifyAndUseDynamicCode(req.verify_code(), req.dynamic());
        if (!status.IsSuccess()) {
            return status;
        }

        status = ResetAdminPassword(req.account(), req.new_pwd());
        if (!status.IsSuccess()) {
            return status;
        }

        SLOG_INFO << "Admin reset password request processed for account: " << req.account();
        return Status {};
    }

    // ---- 指纹相关 ----

    // 删除用户指纹模板并清空数据库记录(用于删除指纹接口)
    static Status DeleteFingerprintFromDevice(const models::User &targetUser) {
        if (targetUser.mFingerprintId == 0) {
            return Status {-1, "该用户未录入指纹"};
        }
        auto fingerId = static_cast<uint16_t>(targetUser.mFingerprintId);
        constexpr uint32_t kTimeoutMs = 10000;
        auto delRet = FingerprintBridge::GetInstance().Delete(fingerId, kTimeoutMs);
        if (delRet != FingerprintResult::OK && delRet != FingerprintResult::NotFound) {
            return Status {-1, "系统繁忙，请重试"};
        }
        if (!UserDaoManager::GetInstance().UpdateFingerprintId(targetUser.mAccountId, 0)) {
            return Status {-1, "系统繁忙，请重试"};
        }
        return Status {};
    }

    Status UserService::EnrollFingerprint(const FingerprintEnrollRequest &req, FingerprintEnrollResponse* resp) {
        Status status = UserOperator(req.target_account_id(), req.account_id());
        if (status.GetCode() != 0) {
            return status;
        }

        models::User targetUser = Dao().GetByAccountId(req.target_account_id());
        if (targetUser.mAccountId == 0) {
            return Status {-1, "用户不存在"};
        }

        // 如果该用户之前没有指纹, 则需要判断总指纹数量是否已达上限
        constexpr int64_t kMaxFingerprintCount = 100;
        int64_t fingerprintCount = UserDaoManager::GetInstance().GetFingerprintUserCount();
        if (fingerprintCount >= kMaxFingerprintCount) {
            return Status {-1, "指纹已满，请清理指纹后重试"};
        }

        // 不预先删除旧指纹: 录入时若为同手指则DuplicateFinger报错, 不同手指则成功后覆盖写入
        auto oldFingerId = static_cast<uint16_t>(targetUser.mFingerprintId);
        (void)resp;
        return FingerprintEnroller::GetInstance().StartEnroll(req.target_account_id(), oldFingerId,
                                                              targetUser.mAccount);
    }

    Status UserService::CancelFingerprintEnroll(const FingerprintCancelRequest &req, FingerprintOperateResponse* resp) {
        Status status = UserOperator(req.target_account_id(), req.account_id());
        if (status.GetCode() != 0) {
            return status;
        }
        (void)resp;
        return FingerprintEnroller::GetInstance().Cancel(req.target_account_id());
    }

    Status UserService::DeleteFingerprint(const FingerprintDeleteRequest &req, FingerprintOperateResponse* resp) {
        Status status = UserOperator(req.target_account_id(), req.account_id());
        if (status.GetCode() != 0) {
            return status;
        }

        models::User targetUser = Dao().GetByAccountId(req.target_account_id());
        if (targetUser.mAccountId == 0) {
            return Status {-1, "用户不存在"};
        }

        status = DeleteFingerprintFromDevice(targetUser);
        if (status.GetCode() != 0) {
            return status;
        }

        (void)resp;
        SLOG_INFO << "Fingerprint deleted for user: " << targetUser.mAccount
                  << " fingerId=" << targetUser.mFingerprintId;
        return Status {};
    }

}  // namespace qifeng_ca
