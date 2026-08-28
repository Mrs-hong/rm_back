//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "qifeng_framework/common/logger.h"

#include "common/status.h"
#include "controller/user_controller.h"
#include <qifeng_ca/common.pb.h>

namespace qifeng_ca {

    Status UserController::Login(const qifeng_ca::UserLoginRequest &req, qifeng_ca::UserLoginResponse &resp) {
        SLOG_DEBUG << "User login: " << req.account();

        Status status = mUserService.Login(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status UserController::GuestLogin(const qifeng_ca::Empty &req, qifeng_ca::UserLoginResponse &resp) {
        SLOG_DEBUG << "Guest login";
        (void)req;

        Status status = mUserService.GuestLogin(&resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status UserController::RefreshToken(const qifeng_ca::UserRefreshRequest &req,
                                        qifeng_ca::UserRefreshResponse &resp) {
        SLOG_DEBUG << "Refresh token for account: " << req.account_id();

        Status status = mUserService.RefreshToken(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status UserController::GetUserInfo(const qifeng_ca::AccountRequst &req, qifeng_ca::UserInfoResponse &resp) {
        SLOG_DEBUG << "Get user info";

        Status status = mUserService.GetUserInfo(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status UserController::Logout(const qifeng_ca::LogoutRequest &req, qifeng_ca::Empty &resp) {
        SLOG_DEBUG << "User logout";

        Status status = mUserService.Logout(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status UserController::GetUserList(const qifeng_ca::UserSearchRequest &req,
                                       qifeng_ca::UserSearchListResponse &resp) {
        SLOG_DEBUG << "Get user list, current: " << req.current() << ", page size: " << req.page_size();

        Status status = mUserService.GetUserList(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status UserController::AddUser(const qifeng_ca::UserAddRequest &req, qifeng_ca::Empty &resp) {
        SLOG_DEBUG << "Add user: " << req.account();

        Status status = mUserService.AddUser(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status UserController::UpdateUser(const qifeng_ca::UserUpdateRequest &req, qifeng_ca::Empty &resp) {
        SLOG_DEBUG << "Update user: " << req.account_id();

        Status status = mUserService.UpdateUser(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status UserController::UpdatePwd(const qifeng_ca::UserUpdatePwdRequest &req, qifeng_ca::Empty &resp) {
        SLOG_DEBUG << "Update password";

        Status status = mUserService.UpdatePwd(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status UserController::UpdatePwdById(const qifeng_ca::UserUpdatePwdByIdRequest &req, qifeng_ca::Empty &resp) {
        SLOG_DEBUG << "Update password by account id: " << req.account_id();

        Status status = mUserService.UpdatePwdById(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status UserController::DelUsers(const qifeng_ca::UserDelRequest &req, qifeng_ca::Empty &resp) {
        SLOG_DEBUG << "Delete users, count: " << req.target_account_ids_size();

        Status status = mUserService.DeleteUsers(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status UserController::AdminForgotPassword(const qifeng_ca::Empty &req, AdminForgotPasswordResponse &resp) {
        SLOG_DEBUG << "AdminForgotPassword";
        (void)req;

        Status status = mUserService.AdminForgotPassword(&resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status UserController::AdminResetPassword(const AdminResetPasswordRequest &req, qifeng_ca::Empty &resp) {
        SLOG_DEBUG << "AdminResetPassword for account: " << req.account();
        (void)resp;

        Status status = mUserService.AdminResetPassword(req);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status UserController::EnrollFingerprint(const FingerprintEnrollRequest &req, FingerprintEnrollResponse &resp) {
        SLOG_DEBUG << "EnrollFingerprint for target: " << req.target_account_id();

        Status status = mUserService.EnrollFingerprint(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status UserController::CancelFingerprintEnroll(const FingerprintCancelRequest &req,
                                                   FingerprintOperateResponse &resp) {
        SLOG_DEBUG << "CancelFingerprintEnroll for target: " << req.target_account_id();

        Status status = mUserService.CancelFingerprintEnroll(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

    Status UserController::DeleteFingerprint(const FingerprintDeleteRequest &req, FingerprintOperateResponse &resp) {
        SLOG_DEBUG << "DeleteFingerprint for target: " << req.target_account_id();

        Status status = mUserService.DeleteFingerprint(req, &resp);
        if (status.GetCode() != 0) {
            FLOG_ERROR(status.ToString());
        }

        return status;
    }

}  // namespace qifeng_ca

// 注册控制器
QIFENG_CA_HTTP_METHOD_REGISTRY(qifeng_ca::UserController);
