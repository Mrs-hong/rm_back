/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_COMMON_SECURITY_JWTUTILS_H
#define QIFENG_FRAMEWORK_INCLUDE_COMMON_SECURITY_JWTUTILS_H

#include <cstdint>
#include <map>
#include <string>

namespace common {
    namespace security {

        /**
         * @brief JWT工具类，提供JWT令牌的生成和验证功能
         * 使用HS256算法（HMAC-SHA256）
         */
        class JwtUtils {
        public:
            /**
             * @brief 生成JWT令牌
             * @param userId 用户ID
             * @param secretKey 密钥
             * @param expiresIn 过期时间，单位：秒
             * @param issuer 签发者（可选）
             * @return JWT令牌
             */
            static std::string GenerateToken(uint64_t userId, const std::string& secretKey, int expiresIn = 3600,
                                             const std::string& issuer = "");

            /**
             * @brief 验证JWT令牌
             * @param token JWT令牌
             * @param secretKey 密钥
             * @param outClaims 输出的声明（可选）
             * @return true 令牌有效，false 令牌无效
             */
            static bool VerifyToken(const std::string& token, const std::string& secretKey,
                                    std::map<std::string, std::string>& outClaims);

            /**
             * @brief 从JWT令牌中提取用户ID
             * @param token JWT令牌
             * @param secretKey 密钥
             * @param outUserId 输出的用户ID
             * @return true 成功提取，false 提取失败
             */
            static bool GetUserIdFromToken(const std::string& token, const std::string& secretKey, uint64_t& outUserId);

        private:
            /**
             * @brief Base64 URL编码
             * @param data 要编码的数据
             * @return Base64 URL编码后的字符串
             */
            static std::string Base64UrlEncode(const std::string& data);

            /**
             * @brief Base64 URL解码
             * @param encodedData Base64 URL编码的数据
             * @return 解码后的数据
             */
            static std::string Base64UrlDecode(const std::string& encodedData);

            /**
             * @brief 使用HMAC-SHA256生成签名
             * @param data 要签名的数据
             * @param secretKey 密钥
             * @return 签名值
             */
            static std::string HmacSha256(const std::string& data, const std::string& secretKey);

            /**
             * @brief 解析JSON字符串为声明映射
             * @param jsonStr JSON字符串
             * @param outClaims 输出的声明映射
             * @return true 解析成功，false 解析失败
             */
            static bool ParseJsonToClaims(const std::string& jsonStr, std::map<std::string, std::string>& outClaims);

            /**
             * @brief 构建payload JSON字符串
             * @param userId 用户ID
             * @param expiresIn 过期时间，单位：秒
             * @param issuer 签发者
             * @return payload JSON字符串
             */
            static std::string BuildPayloadJson(uint64_t userId, int expiresIn, const std::string& issuer);
        };

    }  // namespace security
}  // namespace common

#endif  // QIFENG_FRAMEWORK_INCLUDE_COMMON_SECURITY_JWTUTILS_H
