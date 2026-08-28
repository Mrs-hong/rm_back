//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_USER_CONTROLLER_H
#define QIFENG_CA_INCLUDE_USER_CONTROLLER_H

#include "drogon/DrObject.h"
#include "qifeng_ca/common.pb.h"
#include "qifeng_ca/user.pb.h"

#include "common/http/http.h"
#include "common/status.h"
#include "core/user/user_service.h"

namespace qifeng_ca {

    class UserController final : public drogon::DrObject<UserController> {
    public:
        QIFENG_CA_METHOD_LIST_BEGIN(UserController);

        QIFENG_CA_METHOD_ADD_NO_FILTER("用户登录", Login, "/web/user/login", drogon::Post);

        QIFENG_CA_METHOD_ADD_NO_FILTER("访客登录", GuestLogin, "/web/user/guestLogin", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD(ActionDesc("刷新Token", false), RefreshToken,
                                    BmsPreAccountIdReq<UserRefreshRequest>, "/web/user/refreshToken", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("修改密码", UpdatePwd, BmsPreAccountIdReq<UserUpdatePwdRequest>,
                                    "/web/user/updatePwd", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("管理员修改密码", UpdatePwdById, BmsPreAccountIdReq<UserUpdatePwdByIdRequest>,
                                    "/web/user/updatePwdById", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD(ActionDesc("获取用户信息", false), GetUserInfo, BmsPreAccountIdReq<AccountRequst>,
                                    "/web/user/getUserInfo", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("用户退出", Logout, BmsPreAccountIdReq<LogoutRequest>, "/web/user/logout",
                                    drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD(ActionDesc("获取用户列表", false), GetUserList,
                                    BmsPreAccountIdReq<UserSearchRequest>, "/web/user/getUserList", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("添加用户", AddUser, BmsPreAccountIdReq<UserAddRequest>, "/web/user/addUser",
                                    drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("更新用户", UpdateUser, BmsPreAccountIdReq<UserUpdateRequest>,
                                    "/web/user/updateUser", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("删除用户", DelUsers, BmsPreAccountIdReq<UserDelRequest>, "/web/user/delUsers",
                                    drogon::Post);

        // 管理员忘记密码/重置密码 - 无需登录（无JWT/RBAC过滤）
        QIFENG_CA_METHOD_ADD_NO_FILTER("管理员忘记密码", AdminForgotPassword, "/sys/user/adminForgotPassword",
                                       drogon::Post);

        QIFENG_CA_METHOD_ADD_NO_FILTER("管理员重置密码", AdminResetPassword, "/sys/user/adminResetPassword",
                                       drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("指纹注册", EnrollFingerprint, BmsPreAccountIdReq<FingerprintEnrollRequest>,
                                    "/web/user/enrollFingerprint", drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("取消指纹录入", CancelFingerprintEnroll,
                                    BmsPreAccountIdReq<FingerprintCancelRequest>, "/web/user/cancelFingerprintEnroll",
                                    drogon::Post);

        QIFENG_CA_METHOD_PREREQ_ADD("删除指纹", DeleteFingerprint, BmsPreAccountIdReq<FingerprintDeleteRequest>,
                                    "/web/user/delFingerprint", drogon::Post);

        QIFENG_CA_METHOD_LIST_END;

        Status Login(const UserLoginRequest &req, UserLoginResponse &resp);

        // 访客登录: 无需密码，直接返回token
        Status GuestLogin(const Empty &req, UserLoginResponse &resp);

        Status RefreshToken(const UserRefreshRequest &req, UserRefreshResponse &resp);

        Status GetUserInfo(const AccountRequst &req, UserInfoResponse &resp);

        Status Logout(const LogoutRequest &req, Empty &resp);

        Status UpdateUser(const UserUpdateRequest &req, Empty &resp);

        Status UpdatePwdById(const UserUpdatePwdByIdRequest &req, Empty &resp);

        Status UpdatePwd(const UserUpdatePwdRequest &req, Empty &resp);

        // 管理员接口

        Status AddUser(const UserAddRequest &req, Empty &resp);

        Status GetUserList(const UserSearchRequest &req, UserSearchListResponse &resp);

        // TODO(yf): 删除用户时，需要删除用户目录
        Status DelUsers(const UserDelRequest &req, Empty &resp);

        // 管理员忘记密码/重置密码
        Status AdminForgotPassword(const Empty &req, AdminForgotPasswordResponse &resp);

        Status AdminResetPassword(const AdminResetPasswordRequest &req, Empty &resp);

        // 指纹注册
        Status EnrollFingerprint(const FingerprintEnrollRequest &req, FingerprintEnrollResponse &resp);

        // 取消指纹录入
        Status CancelFingerprintEnroll(const FingerprintCancelRequest &req, FingerprintOperateResponse &resp);

        // 指纹删除
        Status DeleteFingerprint(const FingerprintDeleteRequest &req, FingerprintOperateResponse &resp);

    private:
        UserService mUserService;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_USER_CONTROLLER_H
