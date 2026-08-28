/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "asr/model/private_include/text_processor.h"

#include <cctype>
#include <cstring>

#include "common/logger.h"

namespace qifeng {
    namespace asr {

        // ── UTF-8 辅助工具实现 ──────────────────────────────────────────────────
        int Utf8CharCount(const std::string& s) {
            int count = 0;
            for (std::size_t i = 0; i < s.size();) {
                unsigned char c = static_cast<unsigned char>(s[i]);
                if ((c & 0xE0) == 0xC0)
                    i += 2;
                else if ((c & 0xF0) == 0xE0)
                    i += 3;
                else if ((c & 0xF8) == 0xF0)
                    i += 4;
                else
                    i += 1;
                count++;
            }
            return count;
        }

        std::string Utf8Tail(const std::string& s, int maxChars) {
            int totalChars = Utf8CharCount(s);
            if (totalChars <= maxChars) {
                return s;
            }
            int skip = totalChars - maxChars;
            int count = 0;
            std::size_t i = 0;
            for (; i < s.size() && count < skip;) {
                unsigned char c = static_cast<unsigned char>(s[i]);
                if ((c & 0xE0) == 0xC0)
                    i += 2;
                else if ((c & 0xF0) == 0xE0)
                    i += 3;
                else if ((c & 0xF8) == 0xF0)
                    i += 4;
                else
                    i += 1;
                count++;
            }
            return s.substr(i);
        }

        std::string_view Utf8Chars::Iterator::operator*() const {
            unsigned char c = *p;
            int len = 1;
            if ((c & 0xE0) == 0xC0)
                len = 2;
            else if ((c & 0xF0) == 0xE0)
                len = 3;
            else if ((c & 0xF8) == 0xF0)
                len = 4;
            return std::string_view(p, len);
        }

        Utf8Chars::Iterator& Utf8Chars::Iterator::operator++() {
            unsigned char c = *p;
            if ((c & 0xE0) == 0xC0)
                p += 2;
            else if ((c & 0xF0) == 0xE0)
                p += 3;
            else if ((c & 0xF8) == 0xF0)
                p += 4;
            else
                p += 1;
            return *this;
        }

        bool Utf8Chars::Iterator::operator!=(const Iterator& other) const {
            return p != other.p;
        }

        Utf8Chars::Iterator Utf8Chars::begin() const {
            return {s.data(), s.data() + s.size()};
        }

        Utf8Chars::Iterator Utf8Chars::end() const {
            return {s.data() + s.size(), s.data() + s.size()};
        }

        // ── NormalizeLanguage: 标准化语言字符串 ──────────────────────────────────
        std::string NormalizeLanguage(const std::string& language) {
            if (language.empty()) {
                return "";
            }
            std::string result = language;
            while (!result.empty() && std::isspace(result.front()))
                result.erase(0, 1);
            while (!result.empty() && std::isspace(result.back()))
                result.pop_back();
            if (!result.empty()) {
                result[0] = static_cast<char>(std::toupper(result[0]));
                for (size_t i = 1; i < result.size(); i++) {
                    result[i] = static_cast<char>(std::tolower(result[i]));
                }
            }
            return result;
        }

        // ── BuildPrompt: 构建 chat template prompt ───────────────────────────────
        std::string BuildPrompt(const std::string& context, const std::string& forceLanguage, const char* asrTextTag) {
            std::string prompt = "<|im_start|>system\n";
            if (!context.empty()) {
                prompt += context;
            }
            prompt += "<|im_end|>\n<|im_start|>user\n<|audio_start|><|audio_end|><|im_end|>\n<|im_start|>assistant\n";
            if (!forceLanguage.empty()) {
                prompt += "language " + forceLanguage;
                prompt += asrTextTag;
            }
            return prompt;
        }

        // ── TruncateAtPunctuation: 字符级标点截断 ──────────────────────────────
        std::string TruncateAtPunctuation(const std::string& text, int maxChars) {
            if (text.empty() || Utf8CharCount(text) <= maxChars) {
                return text;
            }
            std::string tail = Utf8Tail(text, maxChars);
            const std::pair<const char*, int> puncts[] = {
                {"\xE3\x80\x82", 3},  // 。
                {"\xEF\xBC\x81", 3},  // ！
                {"\xEF\xBC\x9F", 3},  // ？
                {"\xEF\xBC\x9B", 3},  // ；
                {"\xE2\x80\xA6", 3},  // …
                {"\n", 1},            // \n
            };
            std::size_t firstPos = std::string::npos;
            int firstLen = 0;
            for (const auto& [p, len] : puncts) {
                std::size_t pos = tail.find(p);
                if (pos != std::string::npos && (firstPos == std::string::npos || pos < firstPos)) {
                    firstPos = pos;
                    firstLen = len;
                }
            }
            if (firstPos != std::string::npos) {
                return tail.substr(firstPos + static_cast<std::size_t>(firstLen));
            }
            return tail;
        }

        // ── 内部统一算法：在 UTF-8 字符序列上检测 pattern 重复并去重 ──────────
        // chars: UTF-8 字符列表（每个元素是一个完整 UTF-8 字符）
        // minRepeat: 触发去重的最小连续重复次数
        // maxPatternChars: pattern 最大字符长度（限制搜索空间）
        // 返回 {是否检测到重复, 去重后的字符串}
        // 去重策略：保留重复起始位置之前的全部内容 + 一个 pattern，递归处理剩余部分
        //
        // 性能优化：unitChars=1（单字符重复）走 O(N) 快路径，避免通用嵌套循环。
        static bool DetectSingleCharRepetition(const std::vector<std::string_view>& chars, int minRepeat,
                                               std::size_t& outRepeatStart, std::size_t& outRepeatEnd) {
            const std::size_t charCount = chars.size();
            if (static_cast<int>(charCount) < minRepeat) {
                return false;
            }
            std::size_t bestStart = 0;
            std::size_t bestLen = 0;
            std::size_t i = 0;
            while (i < charCount) {
                std::size_t j = i + 1;
                while (j < charCount && chars[j] == chars[i]) {
                    ++j;
                }
                std::size_t runLen = j - i;
                if (static_cast<int>(runLen) >= minRepeat && runLen > bestLen) {
                    bestLen = runLen;
                    bestStart = i;
                }
                i = j;
            }
            if (bestLen > 0) {
                outRepeatStart = bestStart;
                outRepeatEnd = bestStart + bestLen;
                return true;
            }
            return false;
        }

        static std::pair<bool, std::string> DedupPatternRepeatsImpl(const std::vector<std::string_view>& chars,
                                                                    int minRepeat, int maxPatternChars,
                                                                    int singleCharMinRepeat = 10) {
            const std::size_t charCount = chars.size();
            // 至少需要 minRepeat 个字符才可能形成重复
            if (static_cast<int>(charCount) < minRepeat * 2) {
                return {false, ""};
            }

            // 快路径：单字符重复（最常见的 ASR 死循环模式），O(N) 扫描
            std::size_t scStart = 0;
            std::size_t scEnd = 0;
            if (DetectSingleCharRepetition(chars, singleCharMinRepeat, scStart, scEnd)) {
                std::vector<std::string_view> remaining(chars.begin() + static_cast<std::ptrdiff_t>(scEnd),
                                                        chars.end());
                auto [subFound, subResult] = DedupPatternRepeatsImpl(remaining, minRepeat, maxPatternChars, singleCharMinRepeat);
                std::string result;
                result.reserve(scStart * 4 + 4 + subResult.size());
                for (std::size_t k = 0; k < scStart; ++k) {
                    result += chars[k];
                }
                result += chars[scStart];  // 保留一个
                result += subResult;
                SLOG_DEBUG << "[DetectRepetition] SINGLE CHAR REPEAT! scStart=" << scStart << " scEnd=" << scEnd
                           << " char=" << chars[scStart];
                return {true, result};
            }

            // 通用路径：多字符 pattern 重复检测
            // 阶梯策略：短重复阈值高（避免误触发自然对话），长重复阈值低（尽早捕获死循环）
            // 1~2字: 5次 | 3~10字: 3次 | 10字以上: 2次
            const std::size_t maxUnit = std::min(static_cast<std::size_t>(maxPatternChars), charCount / minRepeat);
            for (std::size_t unitChars = 2; unitChars <= maxUnit; ++unitChars) {
                int effectiveMinRepeat;
                if (unitChars <= 2) {
                    effectiveMinRepeat = 5;  // 短词重复在自然对话中常见，需要更高阈值
                } else if (unitChars <= 10) {
                    effectiveMinRepeat = minRepeat;  // 3次，中等长度短语
                } else {
                    effectiveMinRepeat = 2;  // 长句重复几乎肯定是死循环，尽早检测
                }
                // [FIX] startOff 逐字符前进（原 +=unitChars 要求重复 pattern 起始位置
                // 恰好落在 unitChars 整数倍边界，前导文本长度不对齐时会漏检整句重复）
                for (std::size_t startOff = 0; startOff + unitChars <= charCount; ++startOff) {
                    // 统计从 startOff 开始的 unit 连续重复次数
                    std::size_t maxRepeat = 0;
                    std::size_t repeatStart = startOff;
                    for (std::size_t i = startOff; i + unitChars <= charCount;) {
                        bool match = true;
                        for (std::size_t k = 0; k < unitChars; ++k) {
                            if (chars[i + k] != chars[startOff + k]) {
                                match = false;
                                break;
                            }
                        }
                        if (match) {
                            std::size_t rep = 0;
                            std::size_t j = i;
                            while (j + unitChars <= charCount) {
                                bool m = true;
                                for (std::size_t k = 0; k < unitChars; ++k) {
                                    if (chars[j + k] != chars[startOff + k]) {
                                        m = false;
                                        break;
                                    }
                                }
                                if (!m)
                                    break;
                                ++rep;
                                j += unitChars;
                            }
                            if (rep > maxRepeat) {
                                maxRepeat = rep;
                                repeatStart = i;
                            }
                            i = j;
                        } else {
                            ++i;
                        }
                    }
                    if (static_cast<int>(maxRepeat) >= effectiveMinRepeat) {
                        // 去重：保留 repeatStart 之前内容 + 一个 unit，递归处理剩余
                        std::size_t endIndex = repeatStart + maxRepeat * unitChars;
                        std::vector<std::string_view> remaining(chars.begin() + endIndex, chars.end());
                        auto [subFound, subResult] = DedupPatternRepeatsImpl(remaining, minRepeat, maxPatternChars, singleCharMinRepeat);
                        std::string result;
                        result.reserve(repeatStart * 4 + unitChars * 4 + subResult.size());
                        for (std::size_t k = 0; k < repeatStart; ++k) {
                            result += chars[k];
                        }
                        for (std::size_t k = 0; k < unitChars; ++k) {
                            result += chars[repeatStart + k];
                        }
                        result += subResult;
                        // [DEBUG] 记录多字符 pattern 重复检测详情，用于排查误检丢句
                        std::string patternPreview;
                        for (std::size_t k = 0; k < unitChars && k < 10; ++k) {
                            patternPreview += chars[repeatStart + k];
                        }
                        if (unitChars > 10) {
                            patternPreview += "...";
                        }
                        SLOG_DEBUG << "[DetectRepetition] MULTI CHAR REPEAT! unitChars=" << unitChars
                                   << " maxRepeat=" << maxRepeat << " effectiveMinRepeat=" << effectiveMinRepeat
                                   << " repeatStart=" << repeatStart
                                   << " endIndex=" << endIndex << " charCount=" << charCount
                                   << " pattern=\"" << patternPreview << "\"";
                        return {true, result};
                    }
                }
            }
            return {false, ""};
        }

        // 将字符串拆分为 UTF-8 字符 string_view 列表
        static std::vector<std::string_view> SplitUtf8Chars(const std::string& s) {
            std::vector<std::string_view> chars;
            chars.reserve(s.size());
            for (std::size_t i = 0; i < s.size();) {
                unsigned char c = static_cast<unsigned char>(s[i]);
                std::size_t charLen = 1;
                if ((c & 0x80) == 0) {
                    charLen = 1;
                } else if ((c & 0xE0) == 0xC0) {
                    charLen = 2;
                } else if ((c & 0xF0) == 0xE0) {
                    charLen = 3;
                } else if ((c & 0xF8) == 0xF0) {
                    charLen = 4;
                }
                if (i + charLen > s.size()) {
                    charLen = 1;
                }
                chars.emplace_back(s.data() + i, charLen);
                i += charLen;
            }
            return chars;
        }

        bool DetectRepetition(const std::string& genText, std::string& outDedup, int minRepeat,
                              std::size_t maxCheckChars) {
            if (genText.empty() || minRepeat < 2) {
                outDedup = genText;
                return false;
            }
            // 检测窗口限制：只检查尾部 maxCheckChars 字节，避免长文本下全量扫描
            // 重复死循环的本质是"最近输出陷入循环"，过旧内容无需重复扫描
            std::string_view checkView(genText);
            std::size_t skippedOffset = 0;
            if (maxCheckChars > 0 && checkView.size() > maxCheckChars) {
                std::size_t offset = checkView.size() - maxCheckChars;
                // UTF-8 边界对齐：若 offset 落在 continuation byte (0x80-0xBF) 上，
                // 前移到下一个 leading byte，避免切断多字节字符导致 SplitUtf8Chars 误解析
                while (offset < checkView.size() && (static_cast<unsigned char>(checkView[offset]) & 0xC0) == 0x80) {
                    ++offset;
                }
                checkView = checkView.substr(offset);
                skippedOffset = offset;
            }
            // FIX P1: 必须保持 string 生命周期，string_view 指向其内部缓冲区
            // 原写法 SplitUtf8Chars(std::string(checkView)) 创建临时 string，语句结束后销毁，chars 全为悬空引用
            std::string checkViewStr(checkView);
            auto chars = SplitUtf8Chars(checkViewStr);
            // 至少需要 6 个字符才可能形成 3 次重复
            if (static_cast<int>(chars.size()) < minRepeat * 2) {
                outDedup = genText;
                return false;
            }
            // maxPatternChars=64 支持检测更长重复短语（如"交通运输部相关负责人介绍..."36字）
            // 阶梯策略：1~2字=5次，3~10字=3次，>10字=2次
            auto [found, result] = DedupPatternRepeatsImpl(chars, minRepeat, 64);
            if (found) {
                // 将去重结果拼回：genText 前缀（未参与检测部分）+ 去重后的尾部
                outDedup = std::string(genText.data(), genText.size() - checkView.size());
                outDedup += result;
                SLOG_DEBUG << "[DetectRepetition] FOUND! genTextLen=" << genText.size()
                           << " dedupLen=" << outDedup.size()
                           << " checkViewLen=" << checkView.size()
                           << " skippedPrefix=" << skippedOffset
                           << " result=" << result;
                return true;
            }
            outDedup = genText;
            return false;
        }

        // ── ParseAsrOutput: 解析模型输出中的 language + text ──────────────────
        std::pair<std::string, std::string> ParseAsrOutput(const std::string& raw, const std::string& userLanguage,
                                                           const char* asrTextTag, const char* langPrefix) {
            if (raw.empty()) {
                return {"", ""};
            }
            std::string s = raw;
            {
                std::string deduped;
                // [DEBUG] 注意：这里 maxCheckChars=0（默认），对整个 raw 全量扫描
                // 离线/实时路径都会调用此处，可能是一个丢句差异点
                if (::qifeng::asr::DetectRepetition(raw, deduped)) {
                    SLOG_DEBUG << "[ParseAsrOutput] DetectRepetition triggered, rawLen=" << raw.size()
                               << " dedupedLen=" << deduped.size()
                               << " raw=" << raw
                               << " deduped=" << deduped;
                    s = std::move(deduped);
                }
            }
            if (!userLanguage.empty()) {
                return {userLanguage, s};
            }
            std::size_t tagPos = s.find(asrTextTag);
            if (tagPos == std::string::npos) {
                return {"", s};
            }

            std::string metaPart = s.substr(0, tagPos);
            std::string textPart = s.substr(tagPos + std::strlen(asrTextTag));
            std::string metaLower = metaPart;
            for (auto& c : metaLower) {
                c = static_cast<char>(std::tolower(c));
            }
            if (metaLower.find("language none") != std::string::npos) {
                return {"", TrimWhitespace(textPart)};
            }
            std::string lang;
            std::size_t langPrefixPos = metaLower.find(langPrefix);
            if (langPrefixPos != std::string::npos) {
                std::size_t valStart = langPrefixPos + std::strlen(langPrefix);
                std::size_t valEnd = valStart;
                while (valEnd < metaPart.size() && !std::isspace(metaPart[valEnd])) {
                    valEnd++;
                }
                std::string val = metaPart.substr(valStart, valEnd - valStart);
                if (!val.empty()) {
                    lang = val;
                    if (!lang.empty()) {
                        lang[0] = static_cast<char>(std::toupper(lang[0]));
                        for (size_t li = 1; li < lang.size(); li++) {
                            lang[li] = static_cast<char>(std::tolower(lang[li]));
                        }
                    }
                }
            }
            textPart = TrimWhitespace(textPart);
            return {lang, textPart};
        }

        // ── TrimWhitespace: 去除首尾空白字符 ───────────────────────────────────
        std::string TrimWhitespace(const std::string& s) {
            std::string t = s;
            while (!t.empty() && (t.front() == ' ' || t.front() == '\n'))
                t.erase(0, 1);
            while (!t.empty() && (t.back() == ' ' || t.back() == '\n'))
                t.pop_back();
            return t;
        }

        // ── DecodeWithFallback: decode with anti-garbled FFFD fallback ─────────
        std::string DecodeWithFallback(TokenizerHandle tokenizer, const uint32_t* tokenIds, int endIdx) {
            std::string prefix;
            while (endIdx > 0) {
                TokenizerDecodeResult* decodeResult =
                    ::tokenizer_decode(tokenizer, tokenIds, static_cast<std::size_t>(endIdx), true);
                if (decodeResult == nullptr) {
                    return "";
                }
                prefix = std::string(decodeResult->text);
                ::free_tokenizer_decode_result(decodeResult);
                if (prefix.find("\xef\xbf\xbd") == std::string::npos) {
                    return prefix;
                }
                endIdx--;
            }
            return "";
        }

        // ── GetRollbackPrefix: encoder/decode/anti-garbled loop ────────────────
        std::string GetRollbackPrefix(const std::string& rawDecoded, int unfixedTokenNum, TokenizerHandle tokenizer) {
            TokenizerEncodeResult* encodeResult = nullptr;
            try {
                encodeResult = ::tokenizer_encode(tokenizer, rawDecoded.c_str(), true);
            } catch (const std::exception& e) {
                SLOG_WARN << "[GetPrefixRollback] tokenizer encode threw exception: " << e.what();
                return "";
            }
            if (encodeResult == nullptr) {
                SLOG_WARN << "[GetPrefixRollback] tokenizer encode failed";
                return "";
            }

            int startIdx = std::max(0, static_cast<int>(encodeResult->len) - unfixedTokenNum);
            std::string prefix =
                DecodeWithFallback(tokenizer, reinterpret_cast<const uint32_t*>(encodeResult->token_ids), startIdx);

            ::free_tokenizer_encode_result(encodeResult);
            return prefix;
        }

        // ── BuildFormattedInput: insert audio_pad tags ─────────────────────────
        std::string BuildFormattedInput(const std::string& input, std::size_t audioBosEndPos, std::size_t segmentSize,
                                        uint32_t audioSegmentTokenLength) {
            constexpr std::string_view audioPadTag {"<|audio_pad|>"};
            const std::size_t totalAudioPadCount = segmentSize * audioSegmentTokenLength;
            const std::size_t padTagSize = audioPadTag.size();
            std::string audioPadSequence(totalAudioPadCount * padTagSize, '\0');
            char* padDst = audioPadSequence.data();
            for (std::size_t i = 0; i < totalAudioPadCount; i++) {
                std::memcpy(padDst + i * padTagSize, audioPadTag.data(), padTagSize);
            }
            std::string formattedInput;
            formattedInput.reserve(input.size() + audioPadSequence.size());
            formattedInput.append(input, 0, audioBosEndPos);
            formattedInput.append(audioPadSequence);
            formattedInput.append(input, audioBosEndPos, std::string::npos);
            return formattedInput;
        }

    }  // namespace asr
}  // namespace qifeng
