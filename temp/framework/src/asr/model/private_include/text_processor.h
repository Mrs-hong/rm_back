#ifndef QIFENG_FRAMEWORK_ASR_MODEL_PRIVATE_INCLUDE_TEXT_PROCESSOR_H
#define QIFENG_FRAMEWORK_ASR_MODEL_PRIVATE_INCLUDE_TEXT_PROCESSOR_H

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tokenizers_c.h"

namespace qifeng {
    namespace asr {

        // ── UTF-8 辅助工具 ──────────────────────────────────────────────────────
        int Utf8CharCount(const std::string& s);
        std::string Utf8Tail(const std::string& s, int maxChars);

        struct Utf8Chars {
            const std::string& s;
            struct Iterator {
                const char* p;
                const char* end;
                std::string_view operator*() const;
                Iterator& operator++();
                bool operator!=(const Iterator& other) const;
            };
            Iterator begin() const;
            Iterator end() const;
        };

        // ── 文本后处理 ──────────────────────────────────────────────────────────
        std::string NormalizeLanguage(const std::string& language);
        std::string TruncateAtPunctuation(const std::string& text, int maxChars = 200);
        // 检测 genText 是否出现重复模式（同一子串连续重复 >=minRepeat 次）
        // 若检测到，返回 true 并通过 outDedup 输出去重后的文本（保留首次出现的片段）
        // maxCheckChars: 仅检测尾部最多 N 字节（0=不限制），限制长文本下的扫描开销
        bool DetectRepetition(const std::string& genText, std::string& outDedup, int minRepeat = 3,
                              std::size_t maxCheckChars = 0);
        std::pair<std::string, std::string> ParseAsrOutput(const std::string& raw, const std::string& userLanguage,
                                                           const char* asrTextTag, const char* langPrefix);
        std::string TrimWhitespace(const std::string& s);
        std::string DecodeWithFallback(TokenizerHandle tokenizer, const uint32_t* tokenIds, int endIdx);
        std::string GetRollbackPrefix(const std::string& rawDecoded, int unfixedTokenNum, TokenizerHandle tokenizer);

        // ── Prompt 构建 ─────────────────────────────────────────────────────────
        std::string BuildPrompt(const std::string& context, const std::string& forceLanguage, const char* asrTextTag);
        std::string BuildFormattedInput(const std::string& input, std::size_t audioBosEndPos, std::size_t segmentSize,
                                        uint32_t audioSegmentTokenLength);

    }  // namespace asr
}  // namespace qifeng

#endif
