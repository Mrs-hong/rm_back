//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_CONFIG_AUTH_CONFIG_H
#define QIFENG_CA_INCLUDE_COMMON_CONFIG_AUTH_CONFIG_H

#include <cstdint>
#include <string>

#include "qifeng_framework/common/config_manager.h"

namespace qifeng_ca {

    class AuthConfig {
    public:
        static AuthConfig &GetInstance() {
            static AuthConfig Instance;
            return Instance;
        }

        std::string GetJwtSecret() const {
            return CONFIG_MANAGER.GetString("auth", "jwt_secret", "");
        }

        std::string GetJwtRefreshSecret() const {
            return CONFIG_MANAGER.GetString("auth", "jwt_refresh_secret", "");
        }

        int32_t GetJwtExpiresIn() const { return CONFIG_MANAGER.GetInt("auth", "jwt_expires_in", 3600 * 8); }

        int32_t GetJwtRefreshExpiresIn() const {
            return CONFIG_MANAGER.GetInt("auth", "jwt_refresh_expires_in", 3600 * 24);
        }

        std::string GetJwtIssuer() const { return CONFIG_MANAGER.GetString("auth", "jwt_issuer", ""); }

        std::string GetAdminKey() const { return CONFIG_MANAGER.GetString("auth", "adminKey", ""); }

        std::string GetDefaultAdminPassword() const {
            return CONFIG_MANAGER.GetString("auth", "default_admin_password", "");
        }

        // 默认15分钟(不小于10s)
        int GetVerifyCodeExpirySeconds() const {
            int limit = CONFIG_MANAGER.GetInt("auth", "verify_code_expiry_seconds", 900);
            return limit < 10 ? 10 : limit;
        }

    private:
        AuthConfig() = default;
        ~AuthConfig() = default;
        AuthConfig(const AuthConfig &) = delete;
        AuthConfig &operator=(const AuthConfig &) = delete;
        AuthConfig(AuthConfig &&) = delete;
        AuthConfig &operator=(AuthConfig &&) = delete;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_CONFIG_AUTH_CONFIG_H
