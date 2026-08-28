/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <unistd.h>

#include "common/security/jwt_utils.h"
#include "json/json.h"

namespace common {
    namespace security {

        namespace {
            // jti序列号: 进程级原子递增, 保证同一微秒内并发签发的令牌互不相同
            uint64_t NextJtiSequence() {
                static std::atomic<uint64_t> seq {0};
                return seq.fetch_add(1, std::memory_order_relaxed) + 1;
            }
        }  // namespace

        std::string JwtUtils::GenerateToken(uint64_t userId, const std::string& secretKey, int expiresIn,
                                            const std::string& issuer) {
            // 构建JWT头部
            std::string header = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";

            // 构建JWT载荷
            std::string payload = BuildPayloadJson(userId, expiresIn, issuer);

            // Base64 URL编码头部和载荷
            std::string encodedHeader = Base64UrlEncode(header);
            std::string encodedPayload = Base64UrlEncode(payload);

            // 构建签名数据
            std::string signatureData = encodedHeader + "." + encodedPayload;

            // 生成HMAC-SHA256签名
            std::string signature = HmacSha256(signatureData, secretKey);

            // Base64 URL编码签名
            std::string encodedSignature = Base64UrlEncode(signature);

            // 拼接JWT令牌
            return encodedHeader + "." + encodedPayload + "." + encodedSignature;
        }

        bool JwtUtils::VerifyToken(const std::string& token, const std::string& secretKey,
                                   std::map<std::string, std::string>& outClaims) {
            // 分割令牌为三部分
            size_t firstDot = token.find('.');
            size_t secondDot = token.find('.', firstDot + 1);

            if (firstDot == std::string::npos || secondDot == std::string::npos || secondDot == token.length() - 1) {
                return false;
            }

            std::string encodedHeader = token.substr(0, firstDot);
            std::string encodedPayload = token.substr(firstDot + 1, secondDot - firstDot - 1);
            std::string encodedSignature = token.substr(secondDot + 1);

            // 验证签名
            std::string signatureData = encodedHeader + "." + encodedPayload;
            std::string expectedSignature = HmacSha256(signatureData, secretKey);
            std::string expectedEncodedSignature = Base64UrlEncode(expectedSignature);

            if (encodedSignature != expectedEncodedSignature) {
                return false;
            }

            // 解码并解析载荷
            std::string payload = Base64UrlDecode(encodedPayload);
            if (!ParseJsonToClaims(payload, outClaims)) {
                return false;
            }

            // 检查过期时间
            auto it = outClaims.find("exp");
            if (it == outClaims.end()) {
                return false;
            }

            try {
                uint64_t exp = std::stoull(it->second);
                // 毫秒级时钟, 提升过期判定精度
                auto now = std::chrono::system_clock::now();
                auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

                if (static_cast<uint64_t>(nowMs) > exp * 1000ULL) {
                    return false;
                }

                // 校验签发时间(容忍5秒时钟漂移): 拒绝签发时间在未来超过容差的令牌
                auto iatIt = outClaims.find("iat");
                if (iatIt != outClaims.end()) {
                    uint64_t iat = std::stoull(iatIt->second);
                    uint64_t iatMs = (iat < 1000000000000ULL) ? iat * 1000ULL : iat / 1000ULL;
                    if (iatMs > static_cast<uint64_t>(nowMs) + 5000ULL) {
                        return false;
                    }
                }
            } catch (...) {
                return false;
            }

            return true;
        }

        bool JwtUtils::GetUserIdFromToken(const std::string& token, const std::string& secretKey, uint64_t& outUserId) {
            std::map<std::string, std::string> claims;
            if (!VerifyToken(token, secretKey, claims)) {
                return false;
            }

            auto it = claims.find("sub");
            if (it == claims.end()) {
                return false;
            }

            try {
                outUserId = std::stoull(it->second);
                return true;
            } catch (...) {
                return false;
            }
        }

        // NOLINTBEGIN
        std::string JwtUtils::Base64UrlEncode(const std::string& data) {
            // Base64编码
            std::string base64;
            base64.resize(((data.size() + 2) / 3) * 4);

            int len =
                EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&base64[0]),
                                reinterpret_cast<const unsigned char*>(data.c_str()), static_cast<int>(data.size()));

            if (len < 0) {
                return "";
            }

            base64.resize(static_cast<size_t>(len));

            // Base64 URL转换
            std::replace(base64.begin(), base64.end(), '+', '-');
            std::replace(base64.begin(), base64.end(), '/', '_');

            // 移除填充
            base64.erase(std::remove(base64.begin(), base64.end(), '='), base64.end());

            return base64;
        }
        // NOLINTEND

        // NOLINTBEGIN
        std::string JwtUtils::Base64UrlDecode(const std::string& encodedData) {
            // 恢复Base64 URL到标准Base64
            std::string base64 = encodedData;
            std::replace(base64.begin(), base64.end(), '-', '+');
            std::replace(base64.begin(), base64.end(), '_', '/');

            // 添加填充
            size_t padding = (4 - (base64.size() % 4)) % 4;
            base64.append(padding, '=');

            // Base64解码
            std::string decoded;
            decoded.resize((base64.size() / 4) * 3);

            int len = EVP_DecodeBlock(reinterpret_cast<unsigned char*>(&decoded[0]),
                                      reinterpret_cast<const unsigned char*>(base64.c_str()),
                                      static_cast<int>(base64.size()));

            if (len < 0) {
                return "";
            }

            decoded.resize(static_cast<size_t>(len));

            return decoded;
        }
        // NOLINTEND

        // NOLINTBEGIN
        std::string JwtUtils::HmacSha256(const std::string& data, const std::string& secretKey) {
            std::array<unsigned char, SHA256_DIGEST_LENGTH> hash {};
            unsigned int hashLen = 0;

            // 使用OpenSSL 1.1.1兼容的HMAC API
            HMAC(EVP_sha256(), secretKey.c_str(), static_cast<int>(secretKey.length()),
                 reinterpret_cast<const unsigned char*>(data.c_str()), data.length(), hash.data(), &hashLen);

            // 直接返回原始的二进制哈希数据
            return std::string(reinterpret_cast<const char*>(hash.data()), hashLen);
        }
        // NOLINTEND

        bool JwtUtils::ParseJsonToClaims(const std::string& jsonStr, std::map<std::string, std::string>& outClaims) {
            Json::Value root;
            Json::Reader reader;

            if (!reader.parse(jsonStr, root)) {
                return false;
            }

            // 遍历所有JSON成员
            for (const auto& member : root.getMemberNames()) {
                if (root[member].isString()) {
                    outClaims[member] = root[member].asString();
                } else if (root[member].isInt64()) {
                    outClaims[member] = std::to_string(root[member].asInt64());
                } else if (root[member].isUInt64()) {
                    outClaims[member] = std::to_string(root[member].asUInt64());
                } else if (root[member].isDouble()) {
                    outClaims[member] = std::to_string(root[member].asDouble());
                } else if (root[member].isBool()) {
                    outClaims[member] = root[member].asBool() ? "true" : "false";
                }
            }

            return true;
        }

        std::string JwtUtils::BuildPayloadJson(uint64_t userId, int expiresIn, const std::string& issuer) {
            Json::Value root;

            // 标准声明
            root["sub"] = static_cast<Json::UInt64>(userId);  // 主题（用户ID）

            // 生成微秒级时间戳, 提升高并发场景下的时间精度
            auto now = std::chrono::system_clock::now();
            auto nowUsec = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
            auto expSeconds = nowUsec / 1000000 + expiresIn;

            root["iat"] = static_cast<Json::Int64>(nowUsec);     // 签发时间(微秒)
            root["exp"] = static_cast<Json::Int64>(expSeconds);  // 过期时间(秒, 遵循RFC 7519 numeric date)

            // jti: 令牌唯一标识 = 微秒时间戳 + 进程ID + 进程内原子序列号
            // 同一进程高并发由序列号区分, 跨进程由时间戳+PID区分, 从根本上避免令牌重复
            root["jti"] = std::to_string(nowUsec) + "_" + std::to_string(static_cast<uint64_t>(::getpid())) + "_" +
                          std::to_string(NextJtiSequence());

            // 可选声明
            if (!issuer.empty()) {
                root["iss"] = issuer;  // 签发者
            }

            // 转换为JSON字符串
            Json::FastWriter writer;
            return writer.write(root);
        }

    }  // namespace security
}  // namespace common
