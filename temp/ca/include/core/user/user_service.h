//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_USER_USER_SERVICE_H
#define QIFENG_CA_INCLUDE_CORE_USER_USER_SERVICE_H

#include "qifeng_ca/common.pb.h"
#include "qifeng_ca/user.pb.h"

#include "common/status.h"
#include "dao_managers/user_dao_manager.h"

namespace qifeng_ca {

    class UserService {
    public:
        UserService() = default;
        ~UserService() = default;

        UserService(const UserService &) = delete;
        UserService &operator=(const UserService &) = delete;
        UserService(UserService &&) noexcept = delete;
        UserService &operator=(UserService &&) noexcept = delete;

        Status Login(const UserLoginRequest &req, UserLoginResponse* resp);

        // 访客登录: 无需密码，直接返回token
        Status GuestLogin(UserLoginResponse* resp);

        Status RefreshToken(const UserRefreshRequest &req, UserRefreshResponse* resp);

        Status GetUserInfo(const AccountRequst &req, UserInfoResponse* resp);

        Status Logout(const LogoutRequest &req, Empty* resp);

        Status GetUserList(const UserSearchRequest &req, UserSearchListResponse* resp);

        Status AddUser(const UserAddRequest &req, Empty* resp);

        Status UpdateUser(const UserUpdateRequest &req, Empty* resp);

        Status UpdatePwd(const UserUpdatePwdRequest &req, Empty* resp);

        Status UpdatePwdById(const UserUpdatePwdByIdRequest &req, Empty* resp);

        Status DeleteUsers(const UserDelRequest &req, Empty* resp);

        Status AdminForgotPassword(AdminForgotPasswordResponse* resp);

        Status AdminResetPassword(const AdminResetPasswordRequest &req);

        // 指纹注册: 启动后台采集流程, 采集结果通过WS推送(WsClientId), 已存在指纹则覆盖
        Status EnrollFingerprint(const FingerprintEnrollRequest &req, FingerprintEnrollResponse* resp);

        // 指纹取消: 取消进行中的指纹录入流程
        Status CancelFingerprintEnroll(const FingerprintCancelRequest &req, FingerprintOperateResponse* resp);

        // 指纹删除: 从设备删除指纹, 清除user表fingerprint_id
        Status DeleteFingerprint(const FingerprintDeleteRequest &req, FingerprintOperateResponse* resp);

    private:
        static UserDaoManager &Dao();

        Status UserOperator(uint64_t accountId, uint64_t operatorAccountId);

        Status ResetAdminPassword(const std::string &adminAccount, const std::string &newPassword);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_USER_USER_SERVICE_H
