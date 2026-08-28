/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 *
 * File: hm_utils.h
 * Description:
 *   HM(Houmo) 推理引擎通用工具函数与数据结构：二进制文件读写、UTF-8 编解码、
 *   argmax 等。相比 Qwen3 示例移除了 Eigen 依赖，argmax 使用简单循环实现。
 */

#ifndef QIFENG_FRAMEWORK_LMS_HM_HM_UTILS_H
#define QIFENG_FRAMEWORK_LMS_HM_HM_UTILS_H

#include <codecvt>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <locale>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "houmo/half/half.hpp"
#include "re2/re2.h"
#include "spdlog/fmt/bundled/args.h"
#include "spdlog/fmt/bundled/core.h"
#include "spdlog/fmt/bundled/format.h"
#include "utfcpp/utf8.h"

using json = nlohmann::json;
namespace qifeng {

    namespace lmshm {

        // 统一使用 fp16（half）作为模型输入/输出与 embedding 权重的数据类型。
        using half_float::half;
        using tensor_type = half;

        /**
         * @brief 一条对话消息，包含角色与内容。
         */
        struct Message {
            std::string role;     // 消息发送者角色（system / user / assistant）
            std::string content;  // 消息内容
        };

        /**
         * @brief 从文件读取全部字节。
         *
         * @param path 文件路径
         * @return std::string 文件内容（二进制安全）
         * @throw std::runtime_error 文件无法打开时抛出
         */
        inline std::string LoadBytesFromFile(const std::string& path) {
            std::ifstream fs(path, std::ios::in | std::ios::binary);
            if (fs.fail()) {
                throw std::runtime_error("Cannot open file: " + path);
            }
            std::string data;
            fs.seekg(0, std::ios::end);
            size_t size = static_cast<size_t>(fs.tellg());
            fs.seekg(0, std::ios::beg);
            data.resize(size);
            fs.read(data.data(), static_cast<std::streamsize>(size));
            return data;
        }

        /**
         * @brief 读取 embedding 权重二进制文件。
         *
         * @tparam T 元素类型（例如 half，即 fp16）
         * @param path 文件路径
         * @param nElemsAlign 在末尾额外对齐（补零）的元素个数
         * @return std::unique_ptr<T[]> 读取到的数据，失败返回 nullptr
         */
        template <typename T>
        std::unique_ptr<T[]> ReadEmbeddingWeight(const std::string& path, size_t nElemsAlign = 0) {
            std::ifstream ifs(path, std::ios::binary);
            if (!ifs) {
                return nullptr;
            }

            ifs.seekg(0, std::ios::end);
            const std::size_t nBytes = static_cast<std::size_t>(ifs.tellg());
            ifs.seekg(0);

            const std::size_t nElem = nBytes / sizeof(T) + nElemsAlign;
            auto ptr = std::make_unique<T[]>(nElem);
            ifs.read(reinterpret_cast<char*>(ptr.get()), static_cast<std::streamsize>(nBytes));
            ifs.close();
            // 末尾对齐部分置零，避免未初始化内存被读到。
            memset(reinterpret_cast<char*>(ptr.get()) + nBytes, 0, nElemsAlign * sizeof(T));
            return ptr;
        }

        /**
         * @brief 将 embedding 权重写入二进制文件。
         *
         * @tparam T 元素类型
         * @param path 输出路径
         * @param embeddingWeights 数据指针
         * @param n 元素个数
         * @return true 成功，false 失败
         */
        template <typename T>
        inline bool WriteEmbeddingWeight(const std::string& path, const T* embeddingWeights, size_t n) {
            if (!embeddingWeights || n == 0) {
                return false;
            }
            std::ofstream ofs(path, std::ios::binary);
            if (!ofs) {
                return false;
            }
            ofs.write(reinterpret_cast<const char*>(embeddingWeights), static_cast<std::streamsize>(n * sizeof(T)));
            return ofs.good();
        }

        /**
         * @brief 计算 UTF-8 字符串对应的 UTF-32 字符个数。
         */
        inline std::size_t Utf8Len(std::string_view u8) {
            std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> conv;
            return conv.from_bytes(u8.data(), u8.data() + u8.size()).size();
        }

        /**
         * @brief 将 UTF-8 字符串转换为 UTF-32 字符串。
         */
        inline std::u32string Utf8ToU32(const std::string& u8) {
            std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> conv;
            return conv.from_bytes(u8);
        }

        /**
         * @brief 将 UTF-32 字符串转换为 UTF-8 字符串。
         */
        inline std::string U32ToUtf8(const std::u32string& u32) {
            std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> conv;
            return conv.to_bytes(u32);
        }

        /**
         * @brief 判断 Unicode 码点是否为有效可见字符（CJK 或 ASCII 字母）。
         */
        inline bool IsValidChar(char32_t cp) noexcept {
            return
                // CJK Unified Ideographs
                (cp >= 0x4E00u && cp <= 0x9FFFu) || (cp >= 0x3400u && cp <= 0x4DBFu) ||
                (cp >= 0x20000u && cp <= 0x2A6DFu) || (cp >= 0x2A700u && cp <= 0x2B73Fu) ||
                (cp >= 0x2B740u && cp <= 0x2B81Fu) || (cp >= 0x2B820u && cp <= 0x2CEAFu) ||
                // CJK Compatibility Ideographs
                (cp >= 0xF900u && cp <= 0xFAFFu) || (cp >= 0x2F800u && cp <= 0x2FA1Fu) ||
                // ASCII Letters
                (cp >= 0x0041u && cp <= 0x005Au) ||  // A-Z
                (cp >= 0x0061u && cp <= 0x007Au);    // a-z
        }

        /**
         * @brief 返回数组中最大元素的下标（argmax）。
         *
         * @tparam T 数值类型（支持 half / float / int32_t 等含 operator> 的类型）
         * @param ptr 数据指针
         * @param n 元素个数
         * @return int 最大元素下标；n == 0 时返回 -1
         */
        template <typename T>
        inline int ArgMax(const T* ptr, std::size_t n) {
            if (ptr == nullptr || n == 0) {
                return -1;
            }
            int best = 0;
            for (std::size_t i = 1; i < n; ++i) {
                if (ptr[i] > ptr[best]) {
                    best = static_cast<int>(i);
                }
            }
            return best;
        }

        /**
         * @brief 去除字符串首尾的空白字符（空格、制表符、换行等）。
         */
        inline std::string Trim(const std::string& s) {
            const std::size_t start = s.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) {
                return std::string();
            }
            const std::size_t end = s.find_last_not_of(" \t\r\n");
            return s.substr(start, end - start + 1);
        }

        /**
         * @brief 返回向量末尾至多 n 个元素（不足 n 个时返回全部）。
         */
        inline std::vector<int32_t> LastN(const std::vector<int32_t>& ids, std::size_t n) {
            const std::size_t start = ids.size() > n ? ids.size() - n : 0;
            return std::vector<int32_t>(ids.begin() + start, ids.end());
        }

        /**
         * @brief 字符串全局替换（用于由 past_conv_cache_* 推导 conv_cache_out_*）。
         */
        inline std::string ReplaceAll(std::string str, const std::string& from, const std::string& to) {
            std::size_t pos = 0;
            while ((pos = str.find(from, pos)) != std::string::npos) {
                str.replace(pos, from.size(), to);
                pos += to.size();
            }
            return str;
        }

        inline std::vector<std::string> ParseKeywords(const std::string& raw) {
            // 1. 尝试定位JSON对象起始（可能模型会输出额外解释）
            std::size_t start = raw.find('{');
            std::size_t end = raw.rfind('}');
            if (start == std::string::npos || end == std::string::npos || start > end) {
                return {};  // 解析失败
            }
            std::string json_str = raw.substr(start, end - start + 1);

            // 2. 解析JSON
            try {
                auto j = json::parse(json_str);
                if (j.contains("keywords") && j["keywords"].is_array()) {
                    auto arr = j["keywords"];
                    std::vector<std::string> result;
                    for (const auto& item : arr) {
                        if (item.is_string()) {
                            result.push_back(item.get<std::string>());
                        }
                    }
                    return result;
                }
            } catch (const json::parse_error& e) {
                // 记录日志，返回空
            }
            return {};
        }

        // 将关键词拼接到 overview 中 "会议纪要" 之后
        inline void SpliceKeywords(std::string& ov, const std::vector<std::string>& kw) {
            const std::string target = "---";
            auto pos = ov.find(target);
            if (pos == std::string::npos || kw.empty()) {
                return;
            }
            std::string kwSection = "\n\n### 关键词：";
            for (std::size_t i = 0; i < kw.size(); ++i) {
                if (i > 0) {
                    kwSection += " ";
                }
                kwSection += kw[i];
            }
            ov.insert(pos + target.size(), kwSection);
        }

        // 替换模板中的 {key} 占位符
        inline std::string ReplacePlaceholders(std::string_view tmpl,
                                               const std::map<std::string_view, std::string_view>& params) {
            fmt::dynamic_format_arg_store<fmt::format_context> store {};
            for (const auto& [key, value] : params) {
                store.push_back(fmt::arg(key.data(), value));
            }

            return fmt::vformat(tmpl, store);
        }

        /// 获取 UTF-8 字符串的字符数（非字节数）。
        inline std::size_t Utf8Length(const std::string& s) {
            return static_cast<std::size_t>(utf8::distance(s.begin(), s.end()));
        }
        // inline std::size_t Utf8Length(std::string_view sv) {
        //     return static_cast<std::size_t>(utf8::distance(sv.begin(), sv.end()));
        // }

    }  // namespace lmshm

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_LMS_HM_HM_UTILS_H