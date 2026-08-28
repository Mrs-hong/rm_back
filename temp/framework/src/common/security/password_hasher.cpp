/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <cctype>
#include <iomanip>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "common/security/password_hasher.h"

namespace common {
    namespace security {

        std::string PasswordHasher::HashPassword(const std::string& password, int iterations, int saltLength,
                                                 int keyLength) {
            // 生成盐值
            std::string salt = GenerateSalt(saltLength);

            // 计算PBKDF2哈希
            std::string hash = PBKDF2(password, salt, iterations, keyLength);

            // 格式化哈希字符串
            std::stringstream ss;
            ss << "pbkdf2-sha256$" << iterations << "$" << ToHex(salt) << "$" << ToHex(hash);

            return ss.str();
        }

        bool PasswordHasher::VerifyPassword(const std::string& password, const std::string& hashedPassword) {
            // 解析哈希字符串
            std::stringstream ss(hashedPassword);
            std::string algorithm;
            std::string iterationsStr;
            std::string saltHex;
            std::string hashHex;

            // 检查格式
            if (!std::getline(ss, algorithm, '$') || algorithm.empty()) {
                return false;
            }

            if (algorithm != "pbkdf2-sha256") {
                return false;
            }

            if (!std::getline(ss, iterationsStr, '$') || iterationsStr.empty()) {
                return false;
            }

            if (!std::getline(ss, saltHex, '$') || saltHex.empty()) {
                return false;
            }

            if (!std::getline(ss, hashHex)) {
                return false;
            }

            // 转换迭代次数
            int iterations = std::stoi(iterationsStr);

            // 转换盐值和哈希值
            std::string salt = FromHex(saltHex);
            std::string expectedHash = FromHex(hashHex);

            // 计算实际哈希
            int keyLength = static_cast<int>(expectedHash.length());
            std::string actualHash = PBKDF2(password, salt, iterations, keyLength);

            // 比较哈希值
            return actualHash == expectedHash;
        }

        std::string PasswordHasher::GenerateSalt(int length) {
            std::vector<unsigned char> saltBytes(static_cast<size_t>(length));

            // 使用OpenSSL生成安全的随机盐值
            if (RAND_bytes(saltBytes.data(), length) != 1) {
                throw std::runtime_error("Failed to generate salt");
            }

            return std::string(saltBytes.begin(), saltBytes.end());
        }

        std::string PasswordHasher::PBKDF2(const std::string& password, const std::string& salt, int iterations,
                                           int keyLength) {
            std::vector<unsigned char> saltBytes(salt.begin(), salt.end());
            std::vector<unsigned char> keyBytes(static_cast<size_t>(keyLength));

            // 使用OpenSSL的PBKDF2函数计算哈希
            if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.length()), saltBytes.data(),
                                  static_cast<int>(saltBytes.size()), iterations, EVP_sha256(), keyLength,
                                  keyBytes.data()) != 1) {
                throw std::runtime_error("Failed to compute PBKDF2 hash");
            }

            return std::string(keyBytes.begin(), keyBytes.end());
        }

        std::string PasswordHasher::ToHex(const std::string& data) {
            std::stringstream ss;
            ss << std::hex << std::setfill('0');

            for (char c : data) {
                ss << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
            }

            return ss.str();
        }

        std::string PasswordHasher::FromHex(const std::string& hex) {
            std::string binary;
            binary.reserve(hex.length() / 2);

            for (size_t i = 0; i < hex.length(); i += 2) {
                std::string byteHex = hex.substr(i, 2);
                unsigned char byte = static_cast<unsigned char>(std::stoi(byteHex, nullptr, 16));
                binary.push_back(static_cast<char>(byte));
            }

            return binary;
        }

    }  // namespace security
}  // namespace common
