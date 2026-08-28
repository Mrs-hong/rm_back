//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <openssl/evp.h>

#include "qifeng_framework/common/logger.h"

#include "common/utils/verify.h"

namespace qifeng_ca {

    // ===================== SHA256 哈希计算 =====================

    // 计算文件的 SHA256 哈希值, 返回小写十六进制字符串(64字符)
    // 失败(文件打开失败/读取错误)返回空串
    static std::string ComputeFileSha256(const std::string &filePath) {
        std::ifstream ifs(filePath, std::ios::binary);
        if (!ifs.is_open()) {
            SLOG_ERROR << "ComputeFileSha256: open file failed: " << filePath;
            return {};
        }

        // 创建 EVP 消息摘要上下文
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (ctx == nullptr) {
            SLOG_ERROR << "ComputeFileSha256: EVP_MD_CTX_new failed";
            return {};
        }

        // 初始化摘要算法为 SHA256
        if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
            SLOG_ERROR << "ComputeFileSha256: EVP_DigestInit_ex failed";
            EVP_MD_CTX_free(ctx);
            return {};
        }

        // 流式读取文件并更新摘要
        constexpr std::size_t kBufSize = static_cast<std::size_t>(64) * 1024U;  // 64KB 缓冲区(堆分配, 避免栈空间超限)
        std::vector<char> buf(kBufSize);
        while (true) {
            ifs.read(buf.data(), static_cast<std::streamsize>(kBufSize));
            std::streamsize bytesRead = ifs.gcount();
            if (bytesRead <= 0) {
                break;
            }
            if (EVP_DigestUpdate(ctx, buf.data(), static_cast<std::size_t>(bytesRead)) != 1) {
                SLOG_ERROR << "ComputeFileSha256: EVP_DigestUpdate failed";
                EVP_MD_CTX_free(ctx);
                return {};
            }
        }

        // 结束摘要计算, 输出最终哈希
        std::array<unsigned char, EVP_MAX_MD_SIZE> hash {};
        unsigned int hashLen = 0;
        if (EVP_DigestFinal_ex(ctx, hash.data(), &hashLen) != 1) {
            SLOG_ERROR << "ComputeFileSha256: EVP_DigestFinal_ex failed";
            EVP_MD_CTX_free(ctx);
            return {};
        }
        EVP_MD_CTX_free(ctx);

        // 转为小写十六进制字符串
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (unsigned int i = 0; i < hashLen; ++i) {
            oss << std::setw(2) << static_cast<unsigned int>(hash[static_cast<std::size_t>(i)]);
        }
        return oss.str();
    }

    // ===================== 期望哈希解析 =====================

    // 从 .sha256 清单文件中读取期望哈希值
    // 兼容两种格式:
    //   1. sha256sum 输出格式: "<hash>  <filename>"
    //   2. 纯哈希文本: "<hash>"
    // 取首个空白分隔的 token, 转小写返回; 失败返回空串
    static std::string ParseExpectedSha256(const std::string &sha256Path) {
        std::ifstream ifs(sha256Path);
        if (!ifs.is_open()) {
            SLOG_ERROR << "ParseExpectedSha256: open file failed: " << sha256Path;
            return {};
        }

        std::string token;
        // >> 运算符自动按空白分隔取首个 token
        if (!(ifs >> token)) {
            SLOG_ERROR << "ParseExpectedSha256: read hash token failed: " << sha256Path;
            return {};
        }

        // 转小写
        std::transform(token.begin(), token.end(), token.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return token;
    }

    // ===================== 升级包完整性校验入口 =====================

    Status Verify::VerifyUpgradePackage(const std::string &packagePath, const std::string &sha256Path) {
        SLOG_INFO << "VerifyUpgradePackage: package=" << packagePath << ", sha256=" << sha256Path;

        if (packagePath.empty() || sha256Path.empty()) {
            return Status {-1, "参数错误"};
        }

        // 1. 计算软件包实际 SHA256
        std::string actualHash = ComputeFileSha256(packagePath);
        if (actualHash.empty()) {
            return Status {-1, "计算软件包SHA256失败"};
        }

        // 2. 读取 sha256 清单文件中的期望哈希
        std::string expectedHash = ParseExpectedSha256(sha256Path);
        if (expectedHash.empty()) {
            return Status {-1, "读取sha256清单文件失败"};
        }

        // 3. 期望哈希应为 64 个十六进制字符(SHA256 输出长度)
        if (expectedHash.size() != 64) {
            SLOG_ERROR << "VerifyUpgradePackage: invalid expected hash, len=" << expectedHash.size()
                       << ", hash=" << expectedHash;
            return Status {-1, "sha256清单文件格式错误"};
        }

        SLOG_INFO << "VerifyUpgradePackage: actual=" << actualHash << ", expected=" << expectedHash;

        // 4. 大小写不敏感比较(已统一转小写)
        if (actualHash != expectedHash) {
            return Status {-1, "软件包SHA256校验失败, 完整性校验未通过"};
        }

        SLOG_INFO << "VerifyUpgradePackage: verify ok, package=" << packagePath;
        return {};
    }

}  // namespace qifeng_ca
