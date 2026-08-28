/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_LMS_TEXT_UTILS_H
#define QIFENG_FRAMEWORK_LMS_TEXT_UTILS_H

#include <string>
#include <vector>

#include "re2/re2.h"

namespace qifeng {
    namespace lms {

        std::string FixMarkdownSpacing(const std::string& input);

        /// 删除文本中的括号序号，如 (1)(2)(3)。
        /// @param text   原始文本
        /// @return 去除括号序号后的文本
        std::string RemoveSerialNumbers(const std::string& text);

        /// 解析模型 JSON 响应，提取 "value" 字段（同时将 JSON 转义序列 \n \t 等转为实际字符）。
        /// 若解析失败或不存在 "value" 字段，返回原始文本。
        /// @param raw   模型原始输出
        /// @return 提取后的文本内容
        std::string ExtractModelResponse(const std::string& raw);

        /// 清洗 LLM 输出中的大模型内部专属标签、思考块、模板标记与 Markdown 围栏。
        /// 处理顺序:
        ///   1. 去除首尾空白
        ///   2. 去除 <|im_start|>…<|im_end|> 块（含内容），处理孤立标签
        ///   3. 去除 start…end 块（含内容），处理孤立标签
        ///   4. 去除 <think>…</think> 块（含内容），处理孤立标签
        ///   5. 去除 ```markdown / ``` 围栏标记
        ///   6. 去除 system/user/assistant 角色名
        ///   7. 最终去除首尾空白
        /// @param text   原始 LLM 输出文本
        /// @return 清洗后的文本
        std::string StripTags(const std::string& text);

        /// @param raw   包含"keywords"字段的json文本
        /// @return 关键词串
        std::vector<std::string> ParseKeywords(const std::string& raw);

        /// 根据文本长度动态计算分块大小（使用 UTF-8 字符长度，不是字节长度）。
        /// 分段策略: 用分段区间 + 整除系数逼近"块数随长度缓慢增长"的目标，
        /// 避免长文本分块过大导致数据丢失，同时控制总结耗时的增速。
        /// @param text  输入文本
        /// @return 分块大小（基于 UTF-8 字符数）
        std::size_t ComputeChunkSize(const std::string& text);

        /// 使用 RE2 实现的全局替换函数。
        /// @param text    原始文本
        /// @param re      RE2 正则表达式
        /// @param replace 替换字符串
        /// @return 替换后的文本
        std::string RE2GlobalReplace(const std::string& text, const RE2& re, const std::string& replace);

        /// 去除字符串首尾的空白字符（空格、制表符、换行符等）
        /// @param s  输入字符串
        /// @return 去除首尾空白后的字符串
        std::string Strip(const std::string& s);

        /// 将文本分割为多个分块。
        /// @param text         原始文本
        /// @param chunkSize    每个分块的最大字符数
        /// @param chunkOverlap 相邻分块的重叠字符数，负数表示使用默认值（chunkSize 的 1%，最小为 1）
        /// @return 分块字符串列表
        std::vector<std::string> SplitText(const std::string& text, int chunkSize, int chunkOverlap = -1);

        /// UTF-8 安全地取前 n 个字符（按字符数截取，非字节数）。
        /// 若字符串长度不足 char_count，返回原字符串。
        /// @param s          输入字符串
        /// @param char_count 要截取的字符数
        /// @return 前 char_count 个字符组成的子串
        std::string Utf8Substring(const std::string& s, std::size_t char_count);

        /// UTF-8 安全地按字节数截断，确保不会截断多字节字符的尾部。
        /// 若字符串长度不超过 max_bytes，返回原字符串。
        /// @param s          输入字符串
        /// @param max_bytes  最大字节数
        /// @return 截断后的字符串（保证末尾字符完整）
        std::string Utf8TruncateBytes(const std::string& s, std::size_t max_bytes);

        /// 获取 UTF-8 字符串的字符数（非字节数）。
        /// @param s  输入字符串
        /// @return UTF-8 字符数
        std::size_t Utf8Length(const std::string& s);

        /// 获取 UTF-8 字符串的字符数（非字节数）。
        /// @param s  输入字符串
        /// @return UTF-8 字符数
        std::size_t Utf8Length(std::string_view sv);

    }  // namespace lms
}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_LMS_TEXT_UTILS_H
