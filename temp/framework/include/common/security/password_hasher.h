/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_COMMON_SECURITY_PASSWORDHASHER_H
#define QIFENG_FRAMEWORK_INCLUDE_COMMON_SECURITY_PASSWORDHASHER_H

#include <string>

namespace common {
    namespace security {

        /**
         * @brief 密码哈希工具类，提供安全的密码哈希和验证功能
         * 使用PBKDF2算法，基于HMAC-SHA256，提供高安全性
         */
        class PasswordHasher {
        public:
            /**
             * @brief 生成密码哈希
             * @param password 原始密码
             * @param iterations 迭代次数，推荐值：100000
             * @param saltLength 盐值长度，单位：字节
             * @param keyLength 哈希值长度，单位：字节
             * @return 格式化的哈希字符串，格式：$pbkdf2-sha256$iterations$salt$hash
             */
            static std::string HashPassword(const std::string& password, int iterations = 100000, int saltLength = 16,
                                            int keyLength = 32);

            /**
             * @brief 验证密码是否匹配哈希
             * @param password 原始密码
             * @param hashedPassword 格式化的哈希字符串
             * @return true 密码匹配，false 密码不匹配
             */
            static bool VerifyPassword(const std::string& password, const std::string& hashedPassword);

        private:
            /**
             * @brief 生成随机盐值
             * @param length 盐值长度，单位：字节
             * @return 随机盐值
             */
            static std::string GenerateSalt(int length);

            /**
             * @brief 对密码进行PBKDF2哈希计算
             * @param password 原始密码
             * @param salt 盐值
             * @param iterations 迭代次数
             * @param keyLength 哈希值长度，单位：字节
             * @return 哈希值
             */
            static std::string PBKDF2(const std::string& password, const std::string& salt, int iterations,
                                      int keyLength);

            /**
             * @brief 将二进制数据转换为十六进制字符串
             * @param data 二进制数据
             * @return 十六进制字符串
             */
            static std::string ToHex(const std::string& data);

            /**
             * @brief 将十六进制字符串转换为二进制数据
             * @param hex 十六进制字符串
             * @return 二进制数据
             */
            static std::string FromHex(const std::string& hex);
        };

    }  // namespace security
}  // namespace common

#endif  // QIFENG_FRAMEWORK_INCLUDE_COMMON_SECURITY_PASSWORDHASHER_H
