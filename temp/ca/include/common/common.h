//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_H
#define QIFENG_CA_INCLUDE_COMMON_H

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <uuid/uuid.h>

#include "qifeng_framework/common/security/jwt_utils.h"
#include "qifeng_framework/common/utils/time.h"

#include "common/config/auth_config.h"
#include "dao_managers/user_dao_manager.h"

namespace qifeng_ca {
    namespace WorkflowTakeName {
        static constexpr std::string_view AuditTask = "Audit";
        static constexpr std::string_view DeviceCollectTask = "DeviceCollectTask";
        static constexpr std::string_view DeviceTask = "DeviceTask";
        static constexpr std::string_view ScheduleDispatch = "ScheduleDispatch";
        static constexpr std::string_view DisplayRefreshTask = "DisplayRefreshTask";
        static constexpr std::string_view CleanupTask = "CleanupTask";
        static constexpr std::string_view OtaDetectTask = "OtaDetectTask";
    }  // namespace WorkflowTakeName

    // 现在只有这三种用户组所以用枚举表示
    enum Authority { ADMINISTRATOR = 1, NORMAL_USER = 2, GUEST = 3 };

    // 获取默认管理员账户ID: 从数据库中查询第一个被创建的group_id=1管理员的ID
    // 管理员账户为系统账户, 创建后不变, 内部带缓存
    inline uint64_t GetDefaultAdminAccountId() {
        return UserDaoManager::GetInstance().GetFirstAdminAccountId();
    }

    // 获取默认访客账户ID: 从数据库中查询第一个被创建的group_id=3访客的ID
    // 访客账户为系统账户, 创建后不变, 内部带缓存
    inline uint64_t GetGuestAccountId() {
        return UserDaoManager::GetInstance().GetFirstGuestAccountId();
    }

    // 校验音频访问权限:
    //   管理员(1)=可访问所有音频
    //   普通用户(2)=可访问自己的音频 + 访客音频
    //   访客(3)=只能访问访客音频
    inline bool IsAudioAccessible(uint64_t requesterAccountId, uint64_t requesterGroupId, uint64_t audioOwnerId) {
        if (requesterGroupId == Authority::ADMINISTRATOR) {
            return true;
        }
        if (requesterGroupId == Authority::NORMAL_USER) {
            return audioOwnerId == requesterAccountId || audioOwnerId == GetGuestAccountId();
        }
        // 访客只能访问访客音频
        return audioOwnerId == GetGuestAccountId();
    }

    inline std::string GetGroupName(uint64_t groupId) {
        switch (static_cast<int>(groupId)) {
            case ADMINISTRATOR:
                return "管理员";
            case GUEST:
                return "访客";
            default:
                return "普通用户";
        }
    }

    // 搜索关键词最大长度(字符数)
    inline constexpr size_t KeywordMaxLength = 64;

    // 校验并处理搜索关键词, 防止LIKE通配符注入:
    //   1. 截断到 KeywordMaxLength 字符
    //   2. 去除控制字符
    //   3. 转义 LIKE 特殊字符(\, %, _), MySQL默认以 \ 作为转义符
    //   返回处理后的关键词, 若输入为空或仅含无效字符则返回空串
    inline std::string SanitizeKeyword(const std::string &keyword) {
        if (keyword.empty()) {
            return {};
        }
        std::string sanitized;
        sanitized.reserve(keyword.size());
        size_t count = 0;
        for (unsigned char ch : keyword) {
            if (count >= KeywordMaxLength) {
                break;
            }
            // 跳过控制字符(0x00-0x1F, 0x7F)
            if (ch < 0x20 || ch == 0x7F) {
                continue;
            }
            // 转义 LIKE 特殊字符
            if (ch == '\\' || ch == '%' || ch == '_') {
                sanitized.push_back('\\');
            }
            sanitized.push_back(static_cast<char>(ch));
            ++count;
        }
        return sanitized;
    }

    // 极小概率碰撞(目前只有账号id生成会使用)
    inline uint64_t GetUuid() {
        // return GetTimeUs();
        uuid_t uuid;
        uuid_generate_random(uuid);

        uint64_t hi {};
        uint64_t lo {};

        memcpy(&hi, uuid, 8);
        memcpy(&lo, uuid + 8, 8);

        return hi ^ lo;
    }

    inline std::string GenUUID() {
        uuid_t uuid;

        std::array<char, 37> buf {};
        uuid_generate_random(uuid);
        uuid_unparse_lower(uuid, buf.data());

        return std::string(buf.data());
    }

    inline std::string GenerateAccessToken(uint64_t accountId) {
        auto &config = AuthConfig::GetInstance();
        return common::security::JwtUtils::GenerateToken(accountId, config.GetJwtSecret(), config.GetJwtExpiresIn());
    }

    inline std::string GenerateRefreshToken(uint64_t accountId) {
        auto &config = AuthConfig::GetInstance();
        return common::security::JwtUtils::GenerateToken(accountId, config.GetJwtRefreshSecret(),
                                                         config.GetJwtRefreshExpiresIn());
    }

    inline bool VerifyRefreshToken(const std::string &token, uint64_t &outAccountId) {
        auto &config = AuthConfig::GetInstance();
        return common::security::JwtUtils::GetUserIdFromToken(token, config.GetJwtRefreshSecret(), outAccountId);
    }
}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_H
