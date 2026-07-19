/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/utils/password.h"

#include <algorithm>
#include <memory>
#include <random>
#include <string>
#include <string_view>

namespace qifeng::scm::utils {
    /**
     * @brief 生成随机密码
     * @param length 密码长度
     * @return std::string 包含大小写字母、数字和特殊字符的随机密码
     *
     * 使用 std::random_device 获取硬件级真随机种子，
     * 结合 std::mt19937 生成均匀分布的强随机密码。
     * 每次程序启动生成的密码序列均不同。
     */
    std::string GenerateRandomPassword(int length) {
        // 四类字符集：大写字母、小写字母、数字、特殊字符
        constexpr std::string_view kUpper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        constexpr std::string_view kLower = "abcdefghijklmnopqrstuvwxyz";
        constexpr std::string_view kDigit = "0123456789";
        constexpr std::string_view kSpecial = "!@#$%^&*()_+-=[]{}|;:,.<>?";
        constexpr std::string_view kCharset = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                              "abcdefghijklmnopqrstuvwxyz"
                                              "0123456789"
                                              "!@#$%^&*()_+-=[]{}|;:,.<>?";

        // 密码最短长度需保证每类字符至少一个
        constexpr int kMinLength = 4;
        if (length < kMinLength) {
            length = kMinLength;
        }

        auto randomDevice = std::make_unique<std::random_device>();
        auto generator = std::make_unique<std::minstd_rand>((*randomDevice)());
        std::uniform_int_distribution<size_t> distribution(0, kCharset.size() - 1);

        // 先从每类字符集中各随机取一个，保证每类至少出现一次
        std::uniform_int_distribution<size_t> upperDist(0, kUpper.size() - 1);
        std::uniform_int_distribution<size_t> lowerDist(0, kLower.size() - 1);
        std::uniform_int_distribution<size_t> digitDist(0, kDigit.size() - 1);
        std::uniform_int_distribution<size_t> specialDist(0, kSpecial.size() - 1);

        std::string password;
        password.reserve(static_cast<size_t>(length));
        password += kUpper[upperDist(*generator)];
        password += kLower[lowerDist(*generator)];
        password += kDigit[digitDist(*generator)];
        password += kSpecial[specialDist(*generator)];

        // 剩余位置从全字符集中随机填充
        for (int i = kMinLength; i < length; ++i) {
            password += kCharset[distribution(*generator)];
        }

        // Fisher-Yates 洗牌，打乱前4个固定位置，避免密码结构可预测
        std::shuffle(password.begin(), password.end(), *generator);
        return password;
    }
}  // namespace qifeng::scm::utils
