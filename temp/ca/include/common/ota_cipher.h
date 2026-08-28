//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_OTA_SERVER_COMMON_OTA_CIPHER_H
#define QIFENG_OTA_SERVER_COMMON_OTA_CIPHER_H

#include <array>

#include <string>

namespace qifeng_ca::ota {

    // 字节位移量: 客户端与服务端必须保持一致, 修改时需同步两端避免解密失败
    inline constexpr int CipherShift = 7;

    // 轻量对称混淆: 字节移位 + hex 编码
    // 仅用于 OTA 链路弱敏感字段防直读(日志/抓包), 非密码学安全方案
    class OtaCipher {
    public:
        /**
         * @brief 对明文做字节位移加密并输出 hex 字符串
         * @param plaintext 待加密明文(二进制安全)
         * @return hex 编码密文, 长度为 2 * plaintext.size()
         *
         * hex 编码确保密文仅含 [0-9a-f], 可直接放入 JSON/URL/日志无需转义
         */
        static std::string Encrypt(const std::string &plaintext) {
            std::string out;
            out.reserve(plaintext.size() * 2);
            // char 可能 signed, 显式转 unsigned 后位移避免符号位扩展导致未定义行为
            for (char c : plaintext) {
                unsigned char shifted = static_cast<unsigned char>(c + CipherShift);
                out.push_back(HexDigits[shifted >> 4]);
                out.push_back(HexDigits[shifted & 0x0F]);
            }
            return out;
        }

        /**
         * @brief 解密 Encrypt 产出的 hex 密文, 还原明文
         * @param ciphertext hex 编码密文, 长度必须为偶数
         * @return 还原后的明文; hex 非法或长度为奇返回空串
         *
         * 非法输入返回空串而非抛异常, 调用方按空串判定失败, 简化上层错误处理
         */
        static std::string Decrypt(const std::string &ciphertext) {
            if ((ciphertext.size() & 1U) != 0U) {
                return {};
            }
            std::string out;
            out.reserve(ciphertext.size() / 2);
            for (std::size_t i = 0; i < ciphertext.size(); i += 2U) {
                int hi = HexValue(ciphertext[i]);
                int lo = HexValue(ciphertext[i + 1]);
                if (hi < 0 || lo < 0) {
                    return {};
                }
                unsigned char shifted = static_cast<unsigned char>((hi << 4) | lo);
                // +256 再截断等价于 mod 256, 避免 shifted < CipherShift 时下溢
                unsigned char ch = static_cast<unsigned char>(shifted - CipherShift);
                out.push_back(static_cast<char>(ch));
            }
            return out;
        }

    private:
        // hex 字符查找表: 索引 0-15 映射到 '0'-'9','a'-'f'
        static constexpr std::array<char, 16> HexDigits = {'0', '1', '2', '3', '4', '5', '6', '7',
                                                           '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

        // 单个 hex 字符转数值, 非法字符返回 -1 供调用方判定
        static int HexValue(char c) {
            if (c >= '0' && c <= '9') {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f') {
                return c - 'a' + 10;
            }
            if (c >= 'A' && c <= 'F') {
                return c - 'A' + 10;
            }
            return -1;
        }
    };

}  // namespace qifeng_ca::ota

#endif  // QIFENG_OTA_SERVER_COMMON_OTA_CIPHER_H