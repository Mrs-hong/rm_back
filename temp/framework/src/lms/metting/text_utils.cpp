/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <algorithm>
#include <nlohmann/json.hpp>

#include "lms/metting/private_include/text_utils.h"
#include "utfcpp/utf8.h"

using json = nlohmann::json;
namespace qifeng {
    namespace lms {

        std::string FixMarkdownSpacing(const std::string& input) {
            std::string result = input;

            // 1. 修复加粗 **
            RE2::GlobalReplace(&result, R"((\*\*)\s*([^\s].*?)\s*(\*\*))", "$1$2$3");

            // // 2. 修复加粗 __（下划线）
            // RE2::GlobalReplace(&result, R"((__)\s*([^\s].*?)\s*(__))", "$1$2$3");

            // // 3. 修复删除线 ~~
            // RE2::GlobalReplace(&result, R"((~~)\s*([^\s].*?)\s*(~~))", "$1$2$3");

            // // 4. 修复斜体 *（注意：先处理 **，避免误匹配）
            // RE2::GlobalReplace(&result, R"((\*)\s*([^\s].*?)\s*(\*))", "$1$2$3");

            // // 5. 修复斜体 _（下划线单线）
            // RE2::GlobalReplace(&result, R"((_)\s*([^\s].*?)\s*(_))", "$1$2$3");

            return result;
        }
        // std::string FixMarkdownSpacing(const std::string& input) {
        //     std::string result = input;

        //     // 1. 双边空格: ** 文字 ** → **文字**
        //     RE2::GlobalReplace(&result, R"((\*{1,3}|__|~~)\s+([^\s].*?)\s+(\*{1,3}|__|~~))", "$1$2$3");

        //     // 2. 仅开头有空格: ** 文字** → **文字**
        //     RE2::GlobalReplace(&result, R"((\*{1,3}|__|~~)\s+([^\s].*?)(\*{1,3}|__|~~))", "$1$2$3");

        //     // 3. 仅结尾有空格: **文字 ** → **文字**
        //     RE2::GlobalReplace(&result, R"((\*{1,3}|__|~~)([^\s].*?)\s+(\*{1,3}|__|~~))", "$1$2$3");

        //     return result;
        // }

        std::string RemoveSerialNumbers(const std::string& text) {
            static RE2 re {R"(\w+[（(]+\d+[)）]:)"};
            return RE2GlobalReplace(text, re, "");
        }

        std::string ExtractModelResponse(const std::string& raw) {
            // 查找 {"type": "text" 模式定位 JSON 起始（从后往前，避免匹配到 thinking 中的 {）
            const std::string pattern = R"({"type": "text")";
            auto start = raw.rfind(pattern);
            if (start == std::string::npos) {
                return raw;
            }
            // 从 start 位置开始找匹配的 }
            std::size_t end = std::string::npos;
            int depth = 0;
            for (auto i = start; i < raw.size(); ++i) {
                if (raw[i] == '{')
                    depth++;
                if (raw[i] == '}') {
                    depth--;
                    if (depth == 0) {
                        end = i;
                        break;
                    }
                }
            }
            if (end == std::string::npos) {
                return raw;
            }
            try {
                auto j = json::parse(raw.substr(start, end - start + 1));
                if (j.is_object() && j.contains("value") && j["value"].is_string()) {
                    return j["value"].get<std::string>();
                }
            } catch (const json::parse_error&) {
                // JSON 解析失败（如 value 含未转义换行），手动提取
            }
            // 手动提取 "value": " 之后的内容
            const std::string valPattern = R"("value": ")";
            auto valStart = raw.find(valPattern, start);
            if (valStart == std::string::npos) {
                return raw;
            }
            valStart += valPattern.size();
            // value 结束于最后一个 " 之前（即 "} 前的 "）
            auto valEnd = raw.rfind('"', end);
            if (valEnd == std::string::npos || valEnd <= valStart) {
                return raw;
            }
            auto result = raw.substr(valStart, valEnd - valStart);
            // 手动 unescape \n → 换行（json::parse 失败时逃逸序列未处理）
            for (auto pos = result.find("\\n"); pos != std::string::npos; pos = result.find("\\n", pos)) {
                result.replace(pos, 2, "\n");
                pos += 1;
            }
            return result;
        }

        std::string StripTags(const std::string& text) {
            // 局部 lambda: 去除首尾空白
            auto strip = [](std::string s) -> std::string {
                s.erase(s.begin(),
                        std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
                s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(),
                        s.end());
                return s;
            };

            // 通用 lambda: 删除完整块 + 处理孤立标签
            //   1) 删除完整的 open...close 块（含中间内容）
            //   2) 处理孤立的 close 标签: 删除标签及其之前的所有内容
            //   3) 处理孤立的 open 标签: 删除标签及其之后的所有内容
            auto removeBlock = [&strip](std::string& s, const std::string& open_tag, const std::string& close_tag) {
                // 1. 循环删除完整的 open...close 块
                while (true) {
                    auto open_pos = s.find(open_tag);
                    if (open_pos == std::string::npos)
                        break;
                    auto close_pos = s.find(close_tag, open_pos + open_tag.size());
                    if (close_pos == std::string::npos)
                        break;
                    s.erase(open_pos, close_pos + close_tag.size() - open_pos);
                }

                // 2. 处理孤立的 close 标签: 删除标签及其之前的所有内容
                {
                    auto pos = s.find(close_tag);
                    if (pos != std::string::npos) {
                        s.erase(0, pos + close_tag.size());
                        s = strip(s);
                    }
                }

                // 3. 处理孤立的 open 标签: 删除标签及其之后的所有内容
                {
                    auto pos = s.find(open_tag);
                    if (pos != std::string::npos) {
                        s.erase(pos);
                        s = strip(s);
                    }
                }
            };

            // 1. strip
            std::string s = strip(text);
            if (s.empty()) {
                return s;
            }

            // 2. 去除 <|im_start|>...<|im_end|> 块（含内容），处理孤立标签
            removeBlock(s, "<|im_start|>", "<|im_end|>");

            // 3. 去除 <think>...</think> 块（含内容），处理孤立标签
            removeBlock(s, "<think>", "</think>");

            // 4. 去除 start 和 end 标记
            {
                static const std::string kStart = "start";
                static const std::string kEnd = "end";
                for (const auto& tag : {kStart, kEnd}) {
                    while (true) {
                        auto pos = s.find(tag);
                        if (pos == std::string::npos) {
                            break;
                        }
                        s.erase(pos, tag.size());
                    }
                }
            }

            // 5. 去除 ```markdown 和 ``` 围栏标记
            {
                static const std::string kMdFence = "```markdown";
                static const std::string kFence = "```";
                for (const auto& fence : {kMdFence, kFence}) {
                    while (true) {
                        auto pos = s.find(fence);
                        if (pos == std::string::npos) {
                            break;
                        }
                        s.erase(pos, fence.size());
                    }
                }
            }

            // 5.5 去除 <dict> 和 </dict> 标签
            for (const auto& tag : {std::string("<dict>"), std::string("</dict>")}) {
                while (true) {
                    auto pos = s.find(tag);
                    if (pos == std::string::npos) {
                        break;
                    }
                    s.erase(pos, tag.size());
                }
            }

            // 6. 去除 system/user/assistant 角色名
            {
                static RE2 role_re(R"(\b(system|user|assistant)\b)");
                s = RE2GlobalReplace(s, role_re, "");
            }

            // 7. 提取 <answer> 与 </answer> 之间的内容，去除标签本身及前后所有文本
            {
                static const std::string kAnswerOpen = "<answer>";
                static const std::string kAnswerClose = "</answer>";

                auto pos_open = s.find(kAnswerOpen);     // 第一个开始
                auto pos_close = s.rfind(kAnswerClose);  // 最后一个结束

                if (pos_open != std::string::npos) {
                    // 有开始标签
                    if (pos_close != std::string::npos && pos_close > pos_open) {
                        // 正常情况：开始在前，结束在后 → 取中间（不含标签）
                        s = s.substr(pos_open + kAnswerOpen.size(), pos_close - pos_open - kAnswerOpen.size());
                    } else {
                        // 只有开始（或结束在开始之前，视为无效结束）→ 取开始之后全部
                        s = s.substr(pos_open + kAnswerOpen.size());
                    }
                } else {
                    // 没有开始标签
                    if (pos_close != std::string::npos) {
                        // 只有结束标签 → 取结束之前全部（不含结束标签）
                        s = s.substr(0, pos_close);
                    }
                }
                // 去除标签 <answer>
                while (true) {
                    auto pos = s.find(kAnswerOpen);
                    if (pos == std::string::npos)
                        break;
                    s.erase(pos, kAnswerOpen.size());
                }
                // 去除标签 </answer>
                while (true) {
                    auto pos = s.find(kAnswerClose);
                    if (pos == std::string::npos)
                        break;
                    s.erase(pos, kAnswerClose.size());
                }
            }
            // 8. 截取第一个 '#' 及其之后的内容，去掉客套前缀
            {
                auto pos = s.find('#');
                if (pos != std::string::npos) {
                    s = s.substr(pos);
                }
            }
            // 9. 最终 strip
            s = strip(s);

            return s;
        }

        std::vector<std::string> ParseKeywords(const std::string& raw) {
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

    
        /// 根据文本长度动态计算分块大小。
        /// 分段策略: 用分段区间 + 整除系数逼近"块数随长度缓慢增长"的目标，
        /// 避免长文本分块过大导致数据丢失，同时控制总结耗时的增速。
        std::size_t ComputeChunkSize(const std::string& text) {
            std::size_t textLength = Utf8Length(text);

            if (textLength < 2000) {
                return textLength;
            } else if (textLength < 10000) {
                return (textLength / 35) * 10;
            } else if (textLength < 14000) {
                return (textLength / 45) * 10;
            } else if (textLength < 20000) {
                return (textLength / 55) * 10;
            } else if (textLength < 30000) {
                return (textLength / 75) * 10;
            } else {
                return (textLength / 85) * 10;
            }
        }

        std::string RE2GlobalReplace(const std::string& text, const RE2& re, const std::string& replace) {
            std::string result = text;
            RE2::GlobalReplace(&result, re, replace);
            return result;
        }

        // 去除字符串首尾的空白字符（空格、制表符、换行符等）
        std::string Strip(const std::string& s) {
            // 定义空白字符集合（包含空格、制表符、换行符、回车符等）
            const std::string whitespace = " \t\n\r\f\v";

            // 查找第一个非空白字符的位置
            auto start = s.find_first_not_of(whitespace);
            if (start == std::string::npos) {
                // 字符串全为空白，返回空字符串
                return "";
            }

            // 查找最后一个非空白字符的位置
            auto end = s.find_last_not_of(whitespace);

            // 截取从 start 到 end（含）的子串
            return s.substr(start, end - start + 1);
        }


        /// 递归字符文本分割器（内部实现）。
        /// 按照指定的分隔符优先级依次尝试分割文本，
        /// 使得每个分块的长度不超过 chunk_size。
        /// 参考 Python langchain 库 RecursiveCharacterTextSplitter 实现:
        /// https://github.com/langchain-ai/langchain/blob/master/libs/text-splitters/langchain_text_splitters/character.py
        class RecursiveCharacterTextSplitter {
        public:
            RecursiveCharacterTextSplitter(int chunkSize, int chunkOverlap,
                                           const std::vector<std::string>& separators = {})
                : mChunkSize {chunkSize}, mChunkOverlap {chunkOverlap},
                  mSeparators {separators.empty() ? std::vector<std::string> {"\n\n", "\n", "。", "?", " "}
                                                  : separators} {
            }

            std::vector<std::string> SplitText(const std::string& text) const {
                return SplitText_(text, mSeparators);
            }

        private:
            /// 按照 Python _split_text 逻辑实现:
            /// 1) 找到第一个匹配文本的分隔符; 2) 用该分隔符分割;
            /// 3) 对每个片段: 若长度 < chunk_size 则缓存, 否则先合并缓存再递归;
            /// 4) 最后合并剩余缓存。
            std::vector<std::string> SplitText_(const std::string& text,
                                                const std::vector<std::string>& separators) const {
                std::vector<std::string> finalChunks;

                // 1. 选择合适的分隔符: 找到第一个在 text 中出现的位置
                std::string separator = separators.back();  // 默认最后一个（通常是 ""）
                std::vector<std::string> newSeparators;     // 剩余分隔符列表

                for (size_t i = 0; i < separators.size(); ++i) {
                    const std::string& s = separators[i];
                    if (s.empty()) {
                        separator = s;
                        newSeparators.clear();
                        break;
                    }
                    if (text.find(s) != std::string::npos) {
                        separator = s;
                        newSeparators.assign(separators.begin() + static_cast<std::ptrdiff_t>(i + 1), separators.end());
                        break;
                    }
                }

                // 2. 分隔文本
                std::vector<std::string> splits = SplitTextWithSeparator_(text, separator);

                // 过滤空串 (Python: [s for s in splits if s])
                {
                    std::vector<std::string> filtered;
                    filtered.reserve(splits.size());
                    for (const auto& s : splits) {
                        if (!s.empty()) {
                            filtered.push_back(s);
                        }
                    }
                    splits = std::move(filtered);
                }

                // 3. 遍历 splits: 短的缓存, 长的递归
                std::vector<std::string> goodSplits;
                // merge 时使用的分隔符: 由于 splits 不包含分隔符, 需要重新插入
                const std::string& mergeSep = separator;

                for (const auto& s : splits) {
                    if (Utf8Length(s) < static_cast<std::size_t>(mChunkSize)) {
                        goodSplits.push_back(s);
                    } else {
                        // 先合并已有的 goodSplits
                        if (!goodSplits.empty()) {
                            auto merged = MergeSplits_(goodSplits, mergeSep);
                            finalChunks.insert(finalChunks.end(), merged.begin(), merged.end());
                            goodSplits.clear();
                        }

                        // 对过长的片段递归处理
                        if (newSeparators.empty()) {
                            finalChunks.push_back(s);
                        } else {
                            auto sub = SplitText_(s, newSeparators);
                            finalChunks.insert(finalChunks.end(), sub.begin(), sub.end());
                        }
                    }
                }

                // 4. 合并剩余的 goodSplits
                if (!goodSplits.empty()) {
                    auto merged = MergeSplits_(goodSplits, mergeSep);
                    finalChunks.insert(finalChunks.end(), merged.begin(), merged.end());
                }

                return finalChunks;
            }

            /// 使用指定分隔符分割文本。
            /// 若分隔符为空, 则按 UTF-8 字符分割 (类似 Python list(text))。
            std::vector<std::string> SplitTextWithSeparator_(const std::string& text,
                                                             const std::string& separator) const {
                std::vector<std::string> result;
                if (separator.empty()) {
                    // 按 UTF-8 字符分割
                    auto it = text.begin();
                    auto end = text.end();
                    while (it != end) {
                        auto start = it;
                        utf8::next(it, end);
                        result.emplace_back(start, it);
                    }
                } else {
                    size_t start = 0;
                    size_t pos;
                    while ((pos = text.find(separator, start)) != std::string::npos) {
                        result.push_back(text.substr(start, pos - start));
                        start = pos + separator.size();
                    }
                    result.push_back(text.substr(start));
                }
                return result;
            }

            /// 参考 Python TextSplitter._merge_splits 实现。
            /// 将小片段合并成大块, 每块不超过 chunk_size, 块间有 chunk_overlap 重叠。
            std::vector<std::string> MergeSplits_(const std::vector<std::string>& splits,
                                                  const std::string& separator) const {
                std::vector<std::string> docs;
                std::vector<std::string> currentDoc;
                std::size_t total = 0;
                std::size_t sepLen = Utf8Length(separator);

                for (const auto& d : splits) {
                    std::size_t len = Utf8Length(d);

                    // 若加入当前 split 会超出 chunk_size 且 currentDoc 非空, 则输出一块
                    if (!currentDoc.empty() && total + len + sepLen > static_cast<std::size_t>(mChunkSize)) {
                        // 生成当前块
                        std::string doc = JoinDocs_(currentDoc, separator);
                        if (!doc.empty()) {
                            docs.push_back(std::move(doc));
                        }

                        // 从 currentDoc 头部弹出, 保持 chunk_overlap
                        // 不断弹出直到剩余内容可以容纳新 split + overlap
                        while (!currentDoc.empty()) {
                            std::size_t firstLen = Utf8Length(currentDoc[0]);
                            std::size_t sepAdj = (currentDoc.size() > 1) ? sepLen : 0;

                            // 检查 overlap 条件:
                            // 如果 total <= chunk_overlap 且加入新 split 不会超出, 则停止弹出
                            if (total <= static_cast<std::size_t>(mChunkOverlap) &&
                                total + len + sepLen <= static_cast<std::size_t>(mChunkSize)) {
                                break;
                            }

                            total -= firstLen + sepAdj;
                            currentDoc.erase(currentDoc.begin());
                        }
                    }

                    // 加入当前 split
                    currentDoc.push_back(d);
                    total += len + (currentDoc.size() > 1 ? sepLen : 0);
                }

                // 处理最后一块
                if (!currentDoc.empty()) {
                    std::string doc = JoinDocs_(currentDoc, separator);
                    if (!doc.empty()) {
                        docs.push_back(std::move(doc));
                    }
                }

                return docs;
            }

            /// 将片段列表用分隔符拼接成字符串, 类似 Python 的 separator.join(docs)。
            static std::string JoinDocs_(const std::vector<std::string>& docs, const std::string& separator) {
                std::string result;
                for (size_t i = 0; i < docs.size(); ++i) {
                    if (i > 0)
                        result += separator;
                    result += docs[i];
                }
                return result;
            }

            int mChunkSize;
            int mChunkOverlap;
            std::vector<std::string> mSeparators;
        };

        std::vector<std::string> SplitText(const std::string& text, int chunkSize, int chunkOverlap) {
            if (chunkOverlap < 0) {
                chunkOverlap = std::max(1, chunkSize / 100);
            }
            RecursiveCharacterTextSplitter splitter {chunkSize, chunkOverlap, {"\n\n", "\n", "。", "?", " "}};
            return splitter.SplitText(text);
        }

        /// UTF-8 安全地取前 n 个字符。
        /// 使用 utf8::advance 前移迭代器，若字符数不足则返回原字符串。
        std::string Utf8Substring(const std::string& s, std::size_t char_count) {
            if (char_count == 0) {
                return {};
            }
            if (char_count >= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
                throw std::out_of_range {"char_count is out of range"};
            }
            auto actual_len = static_cast<std::size_t>(utf8::distance(s.begin(), s.end()));
            if (char_count >= actual_len) {
                return s;
            }
            auto it = s.begin();
            utf8::advance(it, static_cast<int>(char_count), s.end());
            return std::string(s.begin(), it);
        }

        /// UTF-8 安全地按字节数截断，确保不会截断多字节字符的尾部。
        /// 从 max_bytes 位置向前回退，跳过 trail 字节直到字符起始位置。
        std::string Utf8TruncateBytes(const std::string& s, std::size_t max_bytes) {
            if (s.size() <= max_bytes) {
                return s;
            }
            auto it = s.begin() + static_cast<std::string::difference_type>(max_bytes);
            auto begin = s.begin();
            // 回退到完整字符的起始位置（跳过 trail 字节）
            while (it != begin && utf8::internal::is_trail(static_cast<unsigned char>(*it))) {
                --it;
            }
            return std::string(begin, it);
        }

        /// 获取 UTF-8 字符串的字符数（非字节数）。
        std::size_t Utf8Length(const std::string& s) {
            return static_cast<std::size_t>(utf8::distance(s.begin(), s.end()));
        }

        /// 获取 UTF-8 字符串的字符数（非字节数）。
        std::size_t Utf8Length(std::string_view sv) {
            return static_cast<std::size_t>(utf8::distance(sv.begin(), sv.end()));
        }

    }  // namespace lms
}  // namespace qifeng
