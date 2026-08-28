//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "qifeng_framework/aas/aas_callback.h"
#include "qifeng_framework/common/logger.h"

#include "common/ws/realtime_dispatch_manager.h"
#include "dao/trans_dao.h"
#include "internal/qwen_trans_merger.h"

namespace {

    // 窗口边界判定容差(毫秒): 当DB记录尾与QWen窗口尾相差在此范围内时,
    // 视为QWen窗口已完整覆盖该记录(允许ASR/SV时间戳微小误差),
    // 避免因几毫秒的overshoot把本可替换的记录误判为"超出窗口"而暂存续接, 导致同一音频内容
    // 在相邻窗口/相邻DB记录间被重复转写与写入(文本重复问题)。
    constexpr int64_t WindowBoundaryToleranceMs = 50;

    // ==================== UTF-8 文本处理工具(用于QWen/Paraformer文本分割匹配) ====================

    // 将UTF-8字符串解码为Unicode码点序列(中文按单码点计)
    std::vector<uint32_t> Utf8ToCodepoints(const std::string &s) {
        std::vector<uint32_t> cps;
        cps.reserve(s.size());
        size_t i = 0;
        while (i < s.size()) {
            uint8_t c = static_cast<uint8_t>(s[i]);
            uint32_t cp = 0;
            size_t len = 1;
            if ((c & 0x80) == 0) {
                cp = c;
            } else if ((c & 0xE0) == 0xC0) {
                cp = c & 0x1F;
                len = 2;
            } else if ((c & 0xF0) == 0xE0) {
                cp = c & 0x0F;
                len = 3;
            } else if ((c & 0xF8) == 0xF0) {
                cp = c & 0x07;
                len = 4;
            } else {
                ++i;  // 非法字节跳过
                continue;
            }
            if (i + len > s.size()) {
                break;
            }
            for (size_t k = 1; k < len; ++k) {
                cp = (cp << 6) | (static_cast<uint8_t>(s[i + k]) & 0x3F);
            }
            cps.push_back(cp);
            i += len;
        }
        return cps;
    }

    // 将码点区间[begin,end)编码为UTF-8字符串
    std::string CodepointsToUtf8(const std::vector<uint32_t> &cps, size_t begin, size_t end) {
        std::string out;
        for (size_t i = begin; i < end; ++i) {
            uint32_t cp = cps[i];
            if (cp < 0x80) {
                out.push_back(static_cast<char>(cp));
            } else if (cp < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else if (cp < 0x10000) {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }
        return out;
    }

    // 判断是否为句子结束标点(。！？；… 及英文.!?;)
    bool IsSentenceEndPunct(uint32_t cp) {
        switch (cp) {
            case 0x3002:  // 。
            case 0xFF01:  // ！
            case 0xFF1F:  // ？
            case 0xFF1B:  // ；
            case 0x2026:  // …
            case 0x002E:  // .
            case 0x0021:  // !
            case 0x003F:  // ?
            case 0x003B:  // ;
                return true;
            default:
                return false;
        }
    }

    // 判断是否为标点(用于字符数统计时剔除标点)
    bool IsPunct(uint32_t cp) {
        if (cp >= 0x3000 && cp <= 0x303F) {  // CJK标点
            return true;
        }
        if (cp >= 0xFF00 && cp <= 0xFFEF) {  // 全角标点/符号
            return true;
        }
        if (cp >= 0x2000 && cp <= 0x206F) {  // 通用标点
            return true;
        }
        switch (cp) {
            case 0x0021:  // !
            case 0x0022:  // "
            case 0x0027:  // '
            case 0x0028:  // (
            case 0x0029:  // )
            case 0x002C:  // ,
            case 0x002E:  // .
            case 0x003A:  // :
            case 0x003B:  // ;
            case 0x003F:  // ?
                return true;
            default:
                return false;
        }
    }

    // 判断是否为闭合引号/括号(句末标点后跟随的闭合符号, 应并入该句)
    bool IsClosingPunct(uint32_t cp) {
        switch (cp) {
            case 0x201D:  // "
            case 0x2019:  // '
            case 0x300B:  // 》
            case 0x300D:  // 」
            case 0x300F:  // 』
            case 0xFF09:  // )
            case 0x0022:  // "
            case 0x0029:  // )
                return true;
            default:
                return false;
        }
    }

    // 基于最长公共子序列(LCS)的字符匹配相似度, 范围[0,1], 0=完全不匹配, 1=完全相同
    double CharMatchSimilarity(const std::string &a, const std::string &b) {
        auto ca = Utf8ToCodepoints(a);
        auto cb = Utf8ToCodepoints(b);
        if (ca.empty() || cb.empty()) {
            return 0.0;
        }
        std::vector<size_t> prev(cb.size() + 1, 0);
        std::vector<size_t> cur(cb.size() + 1, 0);
        for (size_t i = 1; i <= ca.size(); ++i) {
            cur[0] = 0;
            for (size_t j = 1; j <= cb.size(); ++j) {
                if (ca[i - 1] == cb[j - 1]) {
                    cur[j] = prev[j - 1] + 1;
                } else {
                    cur[j] = std::max(prev[j], cur[j - 1]);
                }
            }
            std::swap(prev, cur);
        }
        size_t lcs = prev[cb.size()];
        return 2.0 * static_cast<double>(lcs) / static_cast<double>(ca.size() + cb.size());
    }

    // 在ideal切分点附近(±window)寻找最近的句末标点边界; 找不到则返回ideal
    size_t AdjustCutToSentenceBoundary(const std::vector<uint32_t> &cps, size_t ideal, size_t window) {
        if (cps.empty()) {
            return ideal;
        }
        // 理想点前一个码点已是句末标点 → 直接命中
        if (ideal > 0 && ideal <= cps.size() && IsSentenceEndPunct(cps[ideal - 1])) {
            return ideal;
        }
        size_t lo = ideal > window ? ideal - window : 0;
        size_t hi = std::min(cps.size(), ideal + window);
        for (size_t p = lo + 1; p <= hi; ++p) {
            if (p > 0 && p <= cps.size() && IsSentenceEndPunct(cps[p - 1])) {
                return p;
            }
        }
        return ideal;
    }

    // 字符匹配优化分割(主算法):
    // 以各Paraformer段时间长度占比为切分初值, 在每个切分点邻域内用字符匹配相似度(LCS)搜索最优切分位置
    std::vector<std::string> SplitByCharMatch(const std::string &text,
                                              const std::vector<qifeng::aas::AasSegment> &paraSeqs) {
        std::vector<std::string> parts;
        auto cps = Utf8ToCodepoints(text);
        size_t n = paraSeqs.size();
        if (n <= 1 || cps.empty()) {
            return {text};
        }
        // 1) 初值: 按各Paraformer段时间长度占比计算切分点
        std::vector<size_t> cuts;
        cuts.reserve(n - 1);
        int64_t totalDur = 0;
        for (const auto &ps : paraSeqs) {
            totalDur += std::max<int64_t>(1, ps.endTime - ps.startTime);
        }
        int64_t cum = 0;
        for (size_t i = 0; i < n - 1; ++i) {
            cum += std::max<int64_t>(1, paraSeqs[i].endTime - paraSeqs[i].startTime);
            size_t cut =
                static_cast<size_t>(std::round(static_cast<double>(cps.size()) * static_cast<double>(cum) / totalDur));
            if (cut > cps.size()) {
                cut = cps.size();
            }
            if (cut < cuts.size() + 1) {
                cut = cuts.size() + 1;  // 保证切分点严格递增
            }
            cuts.push_back(cut);
        }
        // 2) 每个切分点在其邻域内用字符匹配相似度优化
        constexpr size_t kWindow = 12;
        for (size_t i = 0; i < cuts.size(); ++i) {
            size_t left = (i == 0) ? 0 : cuts[i - 1];
            size_t right = (i + 1 < cuts.size()) ? cuts[i + 1] : cps.size();
            size_t ideal = cuts[i];
            size_t lo = std::max(left + 1, ideal > kWindow ? ideal - kWindow : 0);
            size_t hi = std::min(right, ideal + kWindow);
            if (hi <= lo) {
                continue;
            }
            size_t bestPos = ideal;
            double bestScore = -1.0;
            for (size_t p = lo; p <= hi; ++p) {
                std::string leftText = CodepointsToUtf8(cps, left, p);
                std::string rightText = CodepointsToUtf8(cps, p, right);
                double score = CharMatchSimilarity(leftText, paraSeqs[i].text) +
                               CharMatchSimilarity(rightText, paraSeqs[i + 1].text);
                if (score > bestScore) {
                    bestScore = score;
                    bestPos = p;
                }
            }
            // 邻域内最优点需与左右边界保持至少1个字符, 避免产生空段
            if (bestPos <= left) {
                bestPos = left + 1;
            }
            if (bestPos >= right) {
                bestPos = right - 1;
            }
            cuts[i] = bestPos;
        }
        // 3) 按切分点提取分段
        size_t start = 0;
        for (size_t i = 0; i < cuts.size(); ++i) {
            size_t cut = cuts[i];
            if (cut < start) {
                cut = start;
            }
            if (cut > cps.size()) {
                cut = cps.size();
            }
            parts.push_back(CodepointsToUtf8(cps, start, cut));
            start = cut;
        }
        parts.push_back(CodepointsToUtf8(cps, start, cps.size()));
        return parts;
    }

    // 字符数相似度分割(备选优化):
    // 按各Paraformer段有效字符数(剔除标点)占比切分, 切分点尽量贴近句末标点
    std::vector<std::string> SplitByCharCountSimilarity(const std::string &text,
                                                        const std::vector<qifeng::aas::AasSegment> &paraSeqs) {
        auto cps = Utf8ToCodepoints(text);
        size_t n = paraSeqs.size();
        if (n <= 1 || cps.empty()) {
            return {text};
        }
        // 各段有效字符数(剔除标点)
        std::vector<size_t> weights;
        weights.reserve(n);
        size_t total = 0;
        for (const auto &ps : paraSeqs) {
            auto pcps = Utf8ToCodepoints(ps.text);
            size_t w = 0;
            for (uint32_t cp : pcps) {
                if (!IsPunct(cp)) {
                    ++w;
                }
            }
            if (w == 0) {
                w = 1;  // 防止除零
            }
            weights.push_back(w);
            total += w;
        }
        std::vector<size_t> cuts;
        cuts.reserve(n - 1);
        size_t cum = 0;
        for (size_t i = 0; i < n - 1; ++i) {
            cum += weights[i];
            size_t ideal =
                static_cast<size_t>(std::round(static_cast<double>(cps.size()) * static_cast<double>(cum) / total));
            ideal = AdjustCutToSentenceBoundary(cps, ideal, 6);
            if (ideal < cuts.size() + 1) {
                ideal = cuts.size() + 1;
            }
            if (ideal > cps.size()) {
                ideal = cps.size();
            }
            cuts.push_back(ideal);
        }
        std::vector<std::string> parts;
        size_t start = 0;
        for (size_t cut : cuts) {
            if (cut < start) {
                cut = start;
            }
            parts.push_back(CodepointsToUtf8(cps, start, cut));
            start = cut;
        }
        parts.push_back(CodepointsToUtf8(cps, start, cps.size()));
        return parts;
    }

    // 基础备选方案: 按句子结束标点分句(未分句时返回空)
    std::vector<std::string> SplitByPunctuation(const std::string &text) {
        std::vector<std::string> sentences;
        auto cps = Utf8ToCodepoints(text);
        std::vector<uint32_t> cur;
        for (size_t i = 0; i < cps.size(); ++i) {
            cur.push_back(cps[i]);
            if (IsSentenceEndPunct(cps[i])) {
                // 吞并紧随其后的闭合引号/括号
                while (i + 1 < cps.size() && IsClosingPunct(cps[i + 1])) {
                    cur.push_back(cps[i + 1]);
                    ++i;
                }
                sentences.push_back(CodepointsToUtf8(cur, 0, cur.size()));
                cur.clear();
            }
        }
        if (!cur.empty()) {
            // 末尾残句: 过短则并入上一句, 否则单独成句
            if (sentences.empty()) {
                sentences.push_back(CodepointsToUtf8(cur, 0, cur.size()));
            } else if (cur.size() <= 2) {
                sentences.back() += CodepointsToUtf8(cur, 0, cur.size());
            } else {
                sentences.push_back(CodepointsToUtf8(cur, 0, cur.size()));
            }
        }
        return sentences;
    }

    // 基础备选方案: 按句末标点分句后, 通过合并/拆分将分句数调整到目标段数
    std::vector<std::string> SplitByPunctuationToCount(const std::string &text, size_t n) {
        auto sentences = SplitByPunctuation(text);
        if (sentences.size() == n) {
            return sentences;
        }
        if (sentences.size() > n) {
            // 分句多于目标: 反复合并"最短的相邻两句"直到数量匹配
            while (sentences.size() > n && sentences.size() > 1) {
                size_t bestIdx = 0;
                size_t bestLen = SIZE_MAX;
                for (size_t i = 0; i + 1 < sentences.size(); ++i) {
                    size_t l = sentences[i].size() + sentences[i + 1].size();
                    if (l < bestLen) {
                        bestLen = l;
                        bestIdx = i;
                    }
                }
                sentences[bestIdx] += sentences[bestIdx + 1];
                sentences.erase(sentences.begin() + static_cast<ptrdiff_t>(bestIdx + 1));
            }
            if (sentences.size() == n) {
                return sentences;
            }
            return {};
        }
        // 分句少于目标: 反复从最长的句子中点附近拆分
        while (sentences.size() < n) {
            size_t bestIdx = 0;
            size_t bestLen = 0;
            for (size_t i = 0; i < sentences.size(); ++i) {
                if (sentences[i].size() > bestLen) {
                    bestLen = sentences[i].size();
                    bestIdx = i;
                }
            }
            auto cps = Utf8ToCodepoints(sentences[bestIdx]);
            if (cps.size() <= 2) {
                break;  // 无法再拆
            }
            size_t cut = AdjustCutToSentenceBoundary(cps, cps.size() / 2, 3);
            if (cut <= 1 || cut >= cps.size()) {
                break;
            }
            std::string left = CodepointsToUtf8(cps, 0, cut);
            std::string right = CodepointsToUtf8(cps, cut, cps.size());
            sentences[bestIdx] = left;
            sentences.insert(sentences.begin() + static_cast<ptrdiff_t>(bestIdx + 1), right);
        }
        if (sentences.size() == n) {
            return sentences;
        }
        return {};
    }

    // 最终兜底: 按字符数均匀分配(文本长度足够时保证每段非空)
    std::vector<std::string> FallbackProportionalSplit(const std::string &text, size_t n) {
        auto cps = Utf8ToCodepoints(text);
        std::vector<std::string> parts;
        if (n == 0 || cps.empty()) {
            return {text};
        }
        if (cps.size() <= n) {
            // 文本过短: 前len段各1字符, 其余为空(由调用方校验后走单段兜底)
            size_t i = 0;
            for (size_t k = 0; k < n; ++k) {
                if (i < cps.size()) {
                    parts.push_back(CodepointsToUtf8(cps, i, i + 1));
                    ++i;
                } else {
                    parts.push_back({});
                }
            }
            return parts;
        }
        size_t base = cps.size() / n;
        size_t rem = cps.size() % n;
        size_t start = 0;
        for (size_t k = 0; k < n; ++k) {
            size_t len = base + (k < rem ? 1 : 0);
            parts.push_back(CodepointsToUtf8(cps, start, start + len));
            start += len;
        }
        return parts;
    }

    // 判断文本是否以完整句子结束(跳过尾部闭合引号/括号后检查句末标点)
    // 用于识别QWen 30s窗口边界被截断的残句: 残句不替换, 保留DB原文等下一窗口补全
    bool EndsWithSentencePunct(const std::string &text) {
        auto cps = Utf8ToCodepoints(text);
        size_t i = cps.size();
        while (i > 0) {
            --i;
            if (!IsClosingPunct(cps[i])) {
                break;
            }
        }
        if (i >= cps.size()) {
            return false;
        }
        return IsSentenceEndPunct(cps[i]);
    }

    // 将DB转写记录转换为AasSegment(仅保留时间戳/文本/说话人, 供文本切割算法使用)
    // DB记录经TranscribeHandler落库去重, 每个时间段仅一条, 是QWen匹配的稳定基准
    qifeng::aas::AasSegment TransToAasSegment(const qifeng_ca::models::Trans &t) {
        qifeng::aas::AasSegment seg;
        seg.startTime = t.mStartTime;
        seg.endTime = t.mEndTime;
        seg.text = t.mContent;
        seg.speakerName = t.mSpeakerName;
        return seg;
    }

    // 计算替换/合并后的分段标志(seg_flag=1表示新段落起点):
    // 除沿用原标志外, 当替换文本较长(超过300字)时强制置为分句起点,
    // 避免多句合成的一大段超过前端500字限制。
    bool CalcSegFlag(bool origSegFlag, const std::string &text) {
        if (origSegFlag) {
            return true;
        }
        constexpr size_t ForceSegFlagCps = 300;
        return Utf8ToCodepoints(text).size() > ForceSegFlagCps;
    }

    // 将QWen替换后的分段构建为前端推送消息(与TranscribeHandler::BuildSegmentMessage格式一致)
    std::string BuildReplacedSegmentMessage(const std::string &audioId, const qifeng::aas::AasSegment &seg, uint64_t id,
                                            bool segFlag) {
        std::string json = R"({"heart":false,)";
        if (id == 0) {
            json += R"("id":null,)";
        } else {
            json += R"("id":)" + std::to_string(id) + R"(,)";
        }
        json += R"("aid":")" + audioId + R"(","speaker_name":")" + seg.speakerName + R"(","start_time":)" +
                std::to_string(seg.startTime) + R"(,"end_time":)" + std::to_string(seg.endTime) + R"(,"content":")" +
                seg.text + R"(","seg_flag":)" + (segFlag ? "true" : "false") + R"(,"final":false,"replaced":true})";
        return json;
    }

}  // namespace

namespace qifeng_ca {

    QwenTransMerger::QwenTransMerger(const std::string &audioId, uint64_t accountId)
        : mAudioId(audioId), mAccountId(accountId) {
        SLOG_INFO << "QwenTransMerger: created, audioId=" << mAudioId;
    }

    // 处理一次Qwen结果: 读DB记录 → 合并D缓存 → 残句拼接 → 匹配替换 → 落库+推送前端
    size_t QwenTransMerger::OnQwenResult(const qifeng::aas::AasResult &result) {
        if (result.code != 0) {
            SLOG_ERROR << "QwenTransMerger: QWen result error, code=" << result.code << " msg=" << result.message
                       << ", audioId=" << mAudioId;
            return 0;
        }
        if (result.segments.empty()) {
            return 0;
        }

        std::lock_guard<std::mutex> lock(mMutex);
        std::vector<ReplacedSegment> replaced;
        // [stop后兜底] Flush已执行(任务停止)后到达的QWen结果(最后窗口晚于stop返回):
        // 仍正常读DB匹配替换(Paraformer记录可能已入库), 但未匹配段不再缓存等待后续窗口,
        // 末尾直接兜底插入DB, 保证stop后返回的QWen数据一定写入数据库
        const bool stopped = mStopped;
        if (stopped) {
            SLOG_WARN << "QwenTransMerger: QWen result arrived after flush(stop), audioId=" << mAudioId
                      << " segments=" << result.segments.size() << " (unmatched segs will be inserted to DB directly)";
        }
        // 读取当前DB中的转写记录作为匹配基准
        TransDao dao;
        std::vector<models::Trans> dbSegs;
        auto transList = dao.GetByAudioId(mAccountId, mAudioId);
        for (const auto &t : transList) {
            if (t.mIsDiscard != 0) {
                continue;
            }
            dbSegs.push_back(t);
        }

        // [D缓存] 合并上次未匹配的DB记录缓存(D结果)参与本次匹配:
        // 防止Paraformer滚动更新(删旧插新)后DB中记录缺失导致替换遗漏
        if (!mPendingDbSegs.empty()) {
            size_t before = dbSegs.size();
            for (const auto &pd : mPendingDbSegs) {
                if (mReplacedDbIds.count(pd.mId) != 0) {
                    continue;  // 已被替换/删除, 不再参与
                }
                bool exists = false;
                for (const auto &t : dbSegs) {
                    if (t.mId == pd.mId) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    dbSegs.push_back(pd);
                }
            }
            if (dbSegs.size() > before) {
                std::sort(dbSegs.begin(), dbSegs.end(),
                          [](const models::Trans &a, const models::Trans &b) { return a.mStartTime < b.mStartTime; });
                SLOG_DEBUG << "QwenTransMerger: merge pending db segs (D cache), audioId=" << mAudioId
                           << " merged=" << dbSegs.size() - before << " total=" << dbSegs.size();
            }
        }

        // [Q缓存] 延后一拍再匹配: 处理上一拍暂存的QWen段(此时paraformer已基本落库),
        // 与已入库记录做重叠覆盖, 实现qwen完全替换paraformer(保留qwen全部内容, 不保留paraformer)。
        // 仍未匹配则视为paraformer丢数据, 重试耗尽/stop时兜底处理, 流式不insert。
        ReplayPendingQwenSegs(dbSegs, replaced);

        // 将上一窗口因截断而暂存的QWen文本片段拼接到当前段开头, 保证跨窗口句子完整
        std::vector<qifeng::aas::AasSegment> segs = result.segments;
        if (!mPendingQwenTail.empty() && !segs.empty()) {
            // [C区间去重] 若该tail文本已存在于窗口开盘相邻的上一条DB记录中(该段边界音频
            // 已被paraformer保留或已被前一窗口写入), 则tail为重复内容, 丢弃以避免双写;
            // 否则才prepend续接跨窗口句子。
            bool tailRedundant = false;
            const int64_t headStart = segs.front().startTime;
            for (auto it = dbSegs.rbegin(); it != dbSegs.rend(); ++it) {
                if (it->mEndTime <= headStart + WindowBoundaryToleranceMs) {
                    if (!it->mContent.empty() &&
                        (it->mContent.find(mPendingQwenTail) != std::string::npos ||
                         mPendingQwenTail.find(it->mContent) != std::string::npos)) {
                        tailRedundant = true;
                    }
                    break;
                }
            }
            if (tailRedundant) {
                SLOG_WARN << "QwenTransMerger: drop redundant pending qwen tail (already stored), audioId="
                          << mAudioId << " tail=[" << mPendingQwenTail << "]";
            } else {
                SLOG_DEBUG << "QwenTransMerger: prepend pending qwen tail, audioId=" << mAudioId << " tail=["
                           << mPendingQwenTail << "] head=[" << segs.front().text << "]";
                segs.front().text = mPendingQwenTail + segs.front().text;
            }
            mPendingQwenTail.clear();
        }

        if (stopped) {
            // stop后到达的最终窗口: 立即按当前DB处理(paraformer此时多已入库), 确保qwen数据不丢,
            // 并冲刷全部缓存(含延后段与tail), 保证最终输出=完整qwen内容。
            for (const auto &seg : segs) {
                if (seg.text.empty()) {
                    continue;
                }
                SLOG_DEBUG << "QwenTransMerger: QWen raw segment(stop), audioId=" << mAudioId << " start="
                           << seg.startTime << " end=" << seg.endTime << " text=[" << seg.text << "] speaker=["
                           << seg.speakerName << "]";
                SplitAndReplaceTextBySpeaker(seg, dbSegs, replaced);
            }
            FlushPendingQwenSegs(replaced);
        } else {
            // 非stop: 延后一拍再匹配——当前窗口qwen段先缓存, 下一拍(约30s后, paraformer已基本落库)
            // 再参与匹配(见ReplayPendingQwenSegs)。从而避免"paraformer尚未落库"导致的缓存重试、溢出
            // 与滑窗重叠双写, 使绝大多数qwen段能在paraformer落库后完成一次性完整替换。
            for (const auto &seg : segs) {
                if (seg.text.empty()) {
                    continue;
                }
                SLOG_DEBUG << "QwenTransMerger: QWen raw segment(deferred), audioId=" << mAudioId << " start="
                           << seg.startTime << " end=" << seg.endTime << " text=[" << seg.text << "] speaker=["
                           << seg.speakerName << "]";
                CachePendingQwenSeg(seg, dbSegs, replaced);
            }
        }

        // [D缓存] 计算本次Qwen窗口范围并更新未匹配DB记录缓存, 供下次窗口复用
        int64_t windowStart = segs.front().startTime;
        int64_t windowEnd = segs.front().endTime;
        for (const auto &seg : segs) {
            windowStart = std::min(windowStart, seg.startTime);
            windowEnd = std::max(windowEnd, seg.endTime);
        }
        if (windowStart < windowEnd) {
            UpdatePendingDbSegs(dbSegs, windowStart, windowEnd);
        }

        if (replaced.empty()) {
            return 0;
        }

        SLOG_INFO << "QwenTransMerger: QWen result matched and replaced, audioId=" << mAudioId
                  << " segments=" << replaced.size();
        PushReplacedSegments(replaced);
        // 部分替换累积已随本次落库生效(下轮从DB读到最新文本), 清空避免跨窗口使用陈旧基准
        mPartialTexts.clear();
        return replaced.size();
    }

    // QWen结果与DB记录整合入口(匹配基准为数据库中稳定去重的转写记录):
    // 1) 时间戳范围完全吻合 → 直接替换
    // 2) QWen完全覆盖单条DB记录 → 直接替换(记录尾未达窗口尾=缺失记录未落库 → 暂存等待)
    // 3) 单条记录未被QWen完全覆盖(记录尾超出窗口) → 保留DB原文, 文本暂存待下一窗口续接
    // 4) QWen覆盖多条DB记录 → 按DB记录分句分割QWen连续文本, 仅替换被完全覆盖的记录
    // 5) 未匹配到任何记录 → 暂存缓存, 待缺失记录入库后重放融合
    // 返回true=匹配到至少一条DB记录(完成替换或尾部暂存续接), false=未匹配(已缓存等待)
    bool QwenTransMerger::SplitAndReplaceText(const qifeng::aas::AasSegment &qwenSeg,
                                              const std::vector<models::Trans> &dbSegs,
                                              std::vector<ReplacedSegment> &replaced) {
        // 收集与QWen段时间重叠且未被替换的DB记录(GetByAudioId按时间顺序返回)
        std::vector<models::Trans> matched;
        for (const auto &t : dbSegs) {
            if (mReplacedDbIds.count(t.mId) != 0) {
                continue;  // 已替换的DB记录, 不允许重复替换
            }
            if (t.mStartTime < qwenSeg.endTime && qwenSeg.startTime < t.mEndTime) {
                matched.push_back(t);
            }
        }

        if (matched.empty()) {
            // 未匹配到任何DB记录(Paraformer尚未将覆盖该窗口的记录落库):
            // 不再直接推送dbId=0(文本会永久丢失), 改为暂存缓存, 待缺失记录入库后重放融合
            SLOG_DEBUG << "QwenTransMerger: QWen no db record match, cache pending, audioId=" << mAudioId
                       << " qwen start=" << qwenSeg.startTime << " end=" << qwenSeg.endTime << " text=[" << qwenSeg.text
                       << "]";
            CachePendingQwenSeg(qwenSeg, dbSegs, replaced);
            return false;
        }

        // 时间戳范围完全吻合 → 直接整体替换
        if (matched.size() == 1 && matched[0].mStartTime == qwenSeg.startTime &&
            matched[0].mEndTime == qwenSeg.endTime) {
            ReplaceDbSeg(matched[0], qwenSeg.text, qwenSeg, replaced);
            return true;
        }

        // QWen段完全覆盖单条DB记录 → 直接整体替换
        if (matched.size() == 1 && qwenSeg.startTime <= matched[0].mStartTime &&
            qwenSeg.endTime >= matched[0].mEndTime) {
            // 记录尾未延伸到QWen窗口尾: 该窗口尾部对应的记录尚未落库(时间戳未完整)。
            // 流式: 暂存等待缺失记录入库后融合; stop/冲刷: 已无后续窗口, paraformer记录为陈旧部分文本,
            if (matched[0].mEndTime < qwenSeg.endTime) {
                if (!mStopped) {
                    SLOG_DEBUG << "QwenTransMerger: QWen covers db seg but db range incomplete, cache pending, audioId="
                               << mAudioId << " qwen=" << qwenSeg.startTime << "-" << qwenSeg.endTime
                               << " db=" << matched[0].mStartTime << "-" << matched[0].mEndTime << " text=["
                               << qwenSeg.text << "]";
                    CachePendingQwenSeg(qwenSeg, dbSegs, replaced);
                    return false;
                }
                SLOG_WARN << "QwenTransMerger: [stop] QWen fully replace incomplete db record, audioId=" << mAudioId
                          << " qwen=" << qwenSeg.startTime << "-" << qwenSeg.endTime << " db=" << matched[0].mStartTime
                          << "-" << matched[0].mEndTime << " old=[" << matched[0].mContent << "] new=[" << qwenSeg.text
                          << "]";
                qifeng::aas::AasSegment updated = TransToAasSegment(matched[0]);
                updated.text = qwenSeg.text;
                updated.endTime = qwenSeg.endTime;  // 更新endTime为QWen尾, 保留startTime与name
                mReplacedDbIds.insert(matched[0].mId);
                ReplacedSegment rs {updated,   qwenSeg.startTime, qwenSeg.endTime,
                                    matched[0].mId, matched[0].mSegFlag == 1, matched[0].mContent};
                replaced.push_back(rs);
                return true;
            }
            ReplaceDbSeg(matched[0], qwenSeg.text, qwenSeg, replaced);
            return true;
        }

        // 单条记录未被QWen完全覆盖(记录尾超出QWen窗口):
        // 优化: 若该DB记录已明显固化(paraformer已固化前期结果, 记录尾远超出QWen窗口)
        //      且QWen文本为完整句(非窗口截断残句), 判定存在paraformer结果丢失,
        //      此时用QWen结果完整替换该记录: 仅保留原记录startTime与说话人name,
        //      文本替换为QWen文本, endTime更新为QWen窗口尾。
        // 否则仍视为窗口截断/续接场景, 保留DB原文, QWen文本暂存待下一窗口开头拼接续接。
        if (matched.size() == 1) {
            const int64_t kFrozenThresholdMs = 3000;  // 明显固化判定: DB记录尾超出QWen窗口尾的毫秒阈值
            const bool dbFrozen =
                (matched[0].mEndTime - qwenSeg.endTime) >= kFrozenThresholdMs &&
                EndsWithSentencePunct(qwenSeg.text);
            if (dbFrozen) {
                SLOG_WARN << "QwenTransMerger: QWen fully replace frozen db record (paraformer data lost), audioId="
                          << mAudioId << " qwen=" << qwenSeg.startTime << "-" << qwenSeg.endTime
                          << " db=" << matched[0].mStartTime << "-" << matched[0].mEndTime << " old=["
                          << matched[0].mContent << "] new=[" << qwenSeg.text << "]";
                qifeng::aas::AasSegment updated = TransToAasSegment(matched[0]);
                updated.text = qwenSeg.text;
                updated.endTime = qwenSeg.endTime;  // 更新endTime为QWen窗口尾, 保留startTime与name
                mReplacedDbIds.insert(matched[0].mId);
                ReplacedSegment rs {updated,   qwenSeg.startTime, qwenSeg.endTime,
                                    matched[0].mId, matched[0].mSegFlag == 1, matched[0].mContent};
                replaced.push_back(rs);
                return true;
            }
            SLOG_DEBUG << "QwenTransMerger: QWen window does not cover db seg end, keep db record, audioId=" << mAudioId
                       << " qwen=" << qwenSeg.startTime << "-" << qwenSeg.endTime << " db=" << matched[0].mStartTime
                       << "-" << matched[0].mEndTime << " text=[" << qwenSeg.text << "]";
            // 暂存QWen文本, 供下一窗口开头拼接续接
            AppendPendingQwenTail(qwenSeg.text);
            return true;
        }

        // QWen段覆盖多条DB记录 → 文本分割后逐条替换
        SplitQwenTextToParaformerSegs(qwenSeg, matched, replaced);
        return true;
    }

    // 基于说话人分组的整合替换
    // 优化: 说话人匹配以Paraformer已入库DB记录的说话人信息为准。
    // 将窗口内匹配记录按Paraformer连续同说话人划分run, 每个run独立融合(保留首条+删除其余);
    // 不同说话人run之间互不合并, 保持独立单元呈现。
    // 窗口边界/碎片刻余时回退现有按时间分割逻辑(SplitQwenTextToParaformerSegs / SplitAndReplaceText)。
    // 返回值语义同SplitAndReplaceText
    bool QwenTransMerger::SplitAndReplaceTextBySpeaker(const qifeng::aas::AasSegment &qwenSeg,
                                                       const std::vector<models::Trans> &dbSegs,
                                                       std::vector<ReplacedSegment> &replaced) {
        // 收集与QWen窗口重叠且未被替换的DB记录(GetByAudioId按start_time升序)
        std::vector<models::Trans> matched;
        for (const auto &t : dbSegs) {
            if (mReplacedDbIds.count(t.mId) != 0) {
                continue;  // 已替换/已删除, 跳过
            }
            if (t.mStartTime < qwenSeg.endTime && qwenSeg.startTime < t.mEndTime) {
                matched.push_back(t);
            }
        }

        // 无匹配记录 → 回退时间匹配(SplitAndReplaceText内部会走Q缓存暂存等待缺失记录入库)
        if (matched.empty()) {
            return SplitAndReplaceText(qwenSeg, dbSegs, replaced);
        }

        // 窗口边界防御: 任一记录尾超出QWen窗口(含边界容差, 该记录还包含下一窗口音频内容)时,
        // 不在此做整段融合(会误删后续内容/误配文本), 回退按时间分割, 由后续窗口续接。
        for (const auto &t : matched) {
            if (t.mEndTime > qwenSeg.endTime + WindowBoundaryToleranceMs) {
                SLOG_DEBUG << "QwenTransMerger: speaker-merge fallback to per-record split (record exceeds qwen "
                              "window), audioId="
                           << mAudioId << " qwen=" << qwenSeg.startTime << "-" << qwenSeg.endTime
                           << " db=" << t.mStartTime << "-" << t.mEndTime << " text=[" << t.mContent << "]";
                SplitQwenTextToParaformerSegs(qwenSeg, matched, replaced);
                return true;
            }
        }

        // 按Paraformer连续同说话人划分run(说话人一致且时间连续, 容忍微小时戳间隙; 空说话人各成单条run)
        struct Run {
            std::vector<models::Trans> records;
            std::string speaker;
        };
        std::vector<Run> runs;
        constexpr int64_t kRunGapToleranceMs = 1000;  // 时间连续判定公差
        for (const auto &t : matched) {
            if (runs.empty() || t.mSpeakerName.empty() || runs.back().speaker != t.mSpeakerName ||
                t.mStartTime > runs.back().records.back().mEndTime + kRunGapToleranceMs) {
                runs.push_back(Run {{t}, t.mSpeakerName});
            } else {
                runs.back().records.push_back(t);
            }
        }

        // 单一run(整个窗口同一连续说话人): 直接用QWen整段文本融合该run
        if (runs.size() == 1) {
            if (runs[0].records.size() >= 2) {
                ConsolidateSameSpeakerSegs(qwenSeg, runs[0].records, qwenSeg.text, replaced);
            } else {
                // 单条记录 → 走现有单条时间匹配逻辑
                return SplitAndReplaceText(qwenSeg, dbSegs, replaced);
            }
            return true;
        }

        // 多个run: 将QWen连续文本按run边界分割, 每个run独立融合/替换(不同说话人互不合并)
        std::vector<qifeng::aas::AasSegment> runSegs;
        runSegs.reserve(runs.size());
        for (const auto &r : runs) {
            qifeng::aas::AasSegment s;
            s.startTime = r.records.front().mStartTime;
            s.endTime = r.records.back().mEndTime;
            s.speakerName = r.speaker;
            for (const auto &rec : r.records) {
                s.text += rec.mContent;
            }
            runSegs.push_back(s);
        }
        auto isValid = [&runs](const std::vector<std::string> &parts) {
            if (parts.size() != runs.size()) {
                return false;
            }
            for (const auto &p : parts) {
                if (p.empty()) {
                    return false;
                }
            }
            return true;
        };
        std::vector<std::string> parts = SplitByCharMatch(qwenSeg.text, runSegs);
        if (!isValid(parts)) {
            parts = SplitByCharCountSimilarity(qwenSeg.text, runSegs);
        }
        if (!isValid(parts)) {
            parts = SplitByPunctuationToCount(qwenSeg.text, runs.size());
        }
        if (!isValid(parts)) {
            parts = FallbackProportionalSplit(qwenSeg.text, runs.size());
        }
        if (!isValid(parts)) {
            // 分割极端兜底失败 → 回退按单条记录时间分割
            SLOG_WARN << "QwenTransMerger: multi-run split fallback to per-record, audioId=" << mAudioId;
            SplitQwenTextToParaformerSegs(qwenSeg, matched, replaced);
            return true;
        }

        size_t before = replaced.size();
        for (size_t i = 0; i < runs.size(); ++i) {
            if (runs[i].records.size() >= 2) {
                // 同一说话人多条连续记录 → 融合为单条(保留首条+删除其余)
                ConsolidateSameSpeakerSegs(qwenSeg, runs[i].records, parts[i], replaced);
            } else {
                // 单条记录 → 用该run分配到的QWen文本替换
                ReplaceDbSeg(runs[i].records[0], parts[i], qwenSeg, replaced);
            }
        }
        if (replaced.size() == before) {
            // 全部被碎片防御跳过 → 回退按单条记录时间分割
            SplitQwenTextToParaformerSegs(qwenSeg, matched, replaced);
            return true;
        }
        return true;
    }

    // 将QWen单条文本融合进同一说话人的多条DB记录:
    // 保留第一条记录(更新文本为传入的consolidatedText, 时间保留Paraformer边界: start=首段start, end=最后合并段end),
    // 其余记录标记删除(前端返回空文本)
    // 若存在记录尾超出QWen窗口(该记录还包含下一窗口音频内容), 不做整段融合删除, 回退按时间分割
    void QwenTransMerger::ConsolidateSameSpeakerSegs(const qifeng::aas::AasSegment &qwenSeg,
                                                     const std::vector<models::Trans> &sameSpeaker,
                                                     const std::string &consolidatedText,
                                                     std::vector<ReplacedSegment> &replaced) {
        // 碎片防御: 新文本远短于各记录内容合计时, 视为窗口截断残句, 保留DB原样等下一窗口
        size_t oldLen = 0;
        for (const auto &t : sameSpeaker) {
            oldLen += Utf8ToCodepoints(t.mContent).size();
        }
        size_t newLen = Utf8ToCodepoints(consolidatedText).size();
        if (oldLen > 0 && newLen < oldLen / 3) {
            SLOG_WARN << "QwenTransMerger: skip consolidate (fragment guard), audioId=" << mAudioId
                      << " speaker=" << qwenSeg.speakerName << " oldLen=" << oldLen << " newLen=" << newLen;
            return;
        }

        // 窗口边界防御: 任一记录尾超出QWen窗口(含边界容差, 该记录还包含下一窗口的音频内容)时,
        // 不能整段融合删除(会误删后续内容), 回退按时间分割, 仅替换被窗口完整覆盖的记录
        for (const auto &t : sameSpeaker) {
            if (t.mEndTime > qwenSeg.endTime + WindowBoundaryToleranceMs) {
                SLOG_DEBUG << "QwenTransMerger: consolidate fallback to time split (record exceeds qwen window), "
                              "audioId="
                           << mAudioId << " qwen=" << qwenSeg.startTime << "-" << qwenSeg.endTime
                           << " db=" << t.mStartTime << "-" << t.mEndTime << " text=[" << t.mContent << "]";
                SplitQwenTextToParaformerSegs(qwenSeg, sameSpeaker, replaced);
                return;
            }
        }

        // 选择保留的ID: 优先采用带分句标志(seg_flag=1)的记录ID以承载段落边界(seg_flag),
        // 使前端能正确识别新段落起点; 若出现多个分句记录, 仅保留第一次出现的分句信息;
        // 均无分句标志时退回首条记录。
        // 时间范围统一为: start=第一个分句的起始时间, end=最后一个分句的结束时间。
        size_t keepIdx = 0;
        for (size_t i = 0; i < sameSpeaker.size(); ++i) {
            if (sameSpeaker[i].mSegFlag == 1) {
                keepIdx = i;
                break;  // 多个分句仅保留第一次出现
            }
        }
        const auto &keep = sameSpeaker[keepIdx];
        const auto &first = sameSpeaker[0];
        const auto &last = sameSpeaker.back();
        // 保留Paraformer时间: start=首段start, end=最后合并段end
        // (融合后文本为QWen结果, 但时间沿用Paraformer分句边界, 保持与音频时间线一致)
        qifeng::aas::AasSegment updated = TransToAasSegment(keep);
        updated.text = consolidatedText;
        updated.startTime = first.mStartTime;
        updated.endTime = last.mEndTime;
        mReplacedDbIds.insert(keep.mId);
        ReplacedSegment rs {updated,   qwenSeg.startTime,      qwenSeg.endTime,
                            keep.mId,  CalcSegFlag(keep.mSegFlag == 1, consolidatedText), keep.mContent};
        replaced.push_back(rs);

        // 其余记录删除, 前端返回空文本信息
        // [删除审计日志] 多ID融合删除: 完整输出每个被删ID、文本内容及元数据, 确保操作可追溯、可审计
        //   dbId=被删除DB记录ID range=被删记录时间范围 speaker=[说话人姓名]
        //   speakerLabel=说话人标签ID segFlag=分段标记(1=新段落开始) content=[被删记录原文]
        //   qwen=触发该删除的QWen段时间范围(定位删除原因)
        // 该条记录将被DeleteById硬删除, 且前端会收到空文本通知移除展示, 故原文内容必须留档
        std::string deletedIds;
        for (size_t i = 0; i < sameSpeaker.size(); ++i) {
            if (i == keepIdx) {
                continue;  // 保留该记录不删除
            }
            const auto &t = sameSpeaker[i];
            mReplacedDbIds.insert(t.mId);
            qifeng::aas::AasSegment delSeg = TransToAasSegment(t);
            delSeg.text.clear();  // 空文本, 通知前端移除该ID
            ReplacedSegment delRs {delSeg, qwenSeg.startTime, qwenSeg.endTime, t.mId, t.mSegFlag == 1, t.mContent};
            delRs.deleted = true;
            replaced.push_back(delRs);
            if (!deletedIds.empty()) {
                deletedIds += ",";
            }
            deletedIds += std::to_string(t.mId);
            SLOG_DEBUG << "QwenTransMerger: consolidate delete record, audioId=" << mAudioId << " dbId=" << t.mId
                       << " range=" << t.mStartTime << "-" << t.mEndTime << " speaker=[" << t.mSpeakerName
                       << "] speakerLabel=" << t.mSpeaker << " segFlag=" << t.mSegFlag << " content=[" << t.mContent
                       << "] qwen=" << qwenSeg.startTime << "-" << qwenSeg.endTime;
        }
        // [融合汇总日志] 同一说话人多条DB记录融合为一条的最终结果:
        SLOG_DEBUG << "QwenTransMerger: QWen consolidate speaker segs, audioId=" << mAudioId
                   << " speaker=" << qwenSeg.speakerName << " qwen=" << qwenSeg.startTime << "-" << qwenSeg.endTime
                   << " text=[" << qwenSeg.text << "] keepId=" << keep.mId << " keepRange=" << first.mStartTime << "-"
                   << last.mEndTime << " keepContent=[" << keep.mContent << "] deletedIds=[" << deletedIds << "]";
    }

    // 将QWen连续文本按多条DB记录分割并逐条替换
    // 分割算法优先级: 字符匹配(时间占比初值+字符相似度细化) > 字符数相似度 > 标点分句 > 均匀兜底
    // 仅替换被QWen窗口完全覆盖的记录(endTime <= qwenSeg.endTime);
    // 记录尾超出窗口的记录保留DB原文, 其QWen文本片段暂存, 供下一窗口开头拼接续接
    void QwenTransMerger::SplitQwenTextToParaformerSegs(const qifeng::aas::AasSegment &qwenSeg,
                                                        const std::vector<models::Trans> &matched,
                                                        std::vector<ReplacedSegment> &replaced) {
        const size_t n = matched.size();
        // [切割定位] 打印切割输入: QWen原始文本 + 全部匹配到的DB记录(时间戳+原文)
        SLOG_DEBUG << "QwenTransMerger: split input, audioId=" << mAudioId << " qwen=" << qwenSeg.startTime << "-"
                   << qwenSeg.endTime << " text=[" << qwenSeg.text << "]";
        for (size_t i = 0; i < n; ++i) {
            SLOG_DEBUG << "QwenTransMerger: split db record[" << i << "], audioId=" << mAudioId
                       << " id=" << matched[i].mId << " range=" << matched[i].mStartTime << "-" << matched[i].mEndTime
                       << " text=[" << matched[i].mContent << "]";
        }
        auto isValid = [n](const std::vector<std::string> &parts) {
            if (parts.size() != n) {
                return false;
            }
            for (const auto &p : parts) {
                if (p.empty()) {
                    return false;
                }
            }
            return true;
        };

        // 将DB记录转换为AasSegment供切割算法使用(时间/文本/说话人)
        std::vector<qifeng::aas::AasSegment> segs;
        segs.reserve(n);
        for (const auto &t : matched) {
            segs.push_back(TransToAasSegment(t));
        }

        std::vector<std::string> parts = SplitByCharMatch(qwenSeg.text, segs);
        if (!isValid(parts)) {
            parts = SplitByCharCountSimilarity(qwenSeg.text, segs);
        }
        if (!isValid(parts)) {
            parts = SplitByPunctuationToCount(qwenSeg.text, n);
        }
        if (!isValid(parts)) {
            parts = FallbackProportionalSplit(qwenSeg.text, n);
        }
        if (!isValid(parts)) {
            // 极端兜底: 文本过短无法分出n个非空段, 仅替换第一条记录, 其余保持DB原文, 避免文本丢失/重复
            SLOG_WARN << "QwenTransMerger: split fallback, only replace first db record, audioId=" << mAudioId
                      << " qwen=" << qwenSeg.startTime << "-" << qwenSeg.endTime << " text=[" << qwenSeg.text
                      << "] segs=" << n;
            ReplaceDbSeg(matched[0], qwenSeg.text, qwenSeg, replaced);
            return;
        }

        // 计算可替换记录的数量: 仅替换记录尾不超出QWen窗口(含边界容差)的记录(被完整覆盖)
        size_t replaceCount = n;
        for (size_t i = 0; i < n; ++i) {
            if (matched[i].mEndTime > qwenSeg.endTime + WindowBoundaryToleranceMs) {
                replaceCount = i;
                break;
            }
        }
        // QWen文本以残句结束(ASR在窗口截断处补了假句末标点, 无法可靠识别)时的兜底:
        // 若全部记录均被覆盖但文本仍疑似截断, 保留最后一条DB原文, 待下一窗口补全
        if (replaceCount == n && !EndsWithSentencePunct(qwenSeg.text) && n > 1) {
            replaceCount = n - 1;
        }
        if (replaceCount == 0) {
            // 首条记录就超出窗口: 全部保留DB原文, QWen文本整体暂存待续接
            SLOG_DEBUG << "QwenTransMerger: QWen window covers no db record fully, keep all, audioId=" << mAudioId
                       << " qwen=" << qwenSeg.startTime << "-" << qwenSeg.endTime << " text=[" << qwenSeg.text << "]";
            AppendPendingQwenTail(qwenSeg.text);
            return;
        }

        for (size_t i = 0; i < replaceCount; ++i) {
            // [切割定位] 打印切割结果: 每个切出的part归属到哪条DB记录
            SLOG_DEBUG << "QwenTransMerger: split part[" << i << "] -> db " << matched[i].mStartTime << "-"
                       << matched[i].mEndTime << ", audioId=" << mAudioId << " text=[" << parts[i] << "]";
            ReplaceDbSeg(matched[i], parts[i], qwenSeg, replaced);
        }
        // 超出窗口的记录保留原文, 对应QWen文本片段暂存供下一窗口拼接续接
        if (replaceCount < n) {
            for (size_t i = replaceCount; i < n; ++i) {
                AppendPendingQwenTail(parts[i]);
            }
            SLOG_DEBUG << "QwenTransMerger: QWen window covers " << replaceCount << "/" << n
                       << " db records, keep tail, audioId=" << mAudioId << " qwen=" << qwenSeg.startTime << "-"
                       << qwenSeg.endTime << " tailStart=" << matched[replaceCount].mStartTime;
        }
        SLOG_INFO << "QwenTransMerger: QWen continuous text split into " << replaceCount << "/" << n
                  << " segments, audioId=" << mAudioId << " qwen range=" << qwenSeg.startTime << "-" << qwenSeg.endTime;
    }

    // 用QWen文本替换单条DB记录: 标记已替换(防止重复替换)+收集替换结果
    // 保持记录的时间戳/说话人/ID等元数据不变; 日志打印QWen原始段时间便于核对替换范围
    // 部分覆盖保护: QWen窗口未覆盖记录头部/尾部时, 不做整条全量替换(会丢失未覆盖区间内容),
    // 按时间占比保留DB原文头/尾切片, 仅用QWen文本替换时间重叠区间, 最终文本=DB头切片+QWen文本+DB尾切片
    void QwenTransMerger::ReplaceDbSeg(const models::Trans &dbSeg, const std::string &newText,
                                       const qifeng::aas::AasSegment &qwenSeg, std::vector<ReplacedSegment> &replaced) {
        // 部分覆盖: 记录头早于QWen窗口起点(窗口只覆盖记录中后段)或记录尾晚于QWen窗口终点
        const int64_t dbDur = dbSeg.mEndTime - dbSeg.mStartTime;
        const bool headPartial = dbSeg.mStartTime < qwenSeg.startTime;
        const bool tailPartial = dbSeg.mEndTime > qwenSeg.endTime;
        if (dbDur > 0 && (headPartial || tailPartial)) {
            // 基准文本: 同窗口内已部分替换过该记录时用累积文本切片, 避免覆盖掉本窗口前一段QWen结果
            auto it = mPartialTexts.find(dbSeg.mId);
            const std::string &base = (it != mPartialTexts.end()) ? it->second : dbSeg.mContent;
            auto cps = Utf8ToCodepoints(base);
            auto clamp01 = [](double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); };
            // 头/尾保留长度按未覆盖时间占比折算(重叠区间对应的文本切片才被QWen文本替换)
            double headFrac = clamp01(static_cast<double>(qwenSeg.startTime - dbSeg.mStartTime) / dbDur);
            double tailFrac = clamp01(static_cast<double>(dbSeg.mEndTime - qwenSeg.endTime) / dbDur);
            size_t headLen = static_cast<size_t>(static_cast<double>(cps.size()) * headFrac + 0.5);
            size_t tailLen = static_cast<size_t>(static_cast<double>(cps.size()) * tailFrac + 0.5);
            if (headLen + tailLen > cps.size()) {
                // 重叠区间极小时头尾保留长度可能超出原文: 按比例压缩, 优先保证QWen文本写入
                double scale = static_cast<double>(cps.size()) / static_cast<double>(headLen + tailLen);
                headLen = static_cast<size_t>(static_cast<double>(headLen) * scale);
                tailLen = cps.size() - headLen;
            }
            std::string head = CodepointsToUtf8(cps, 0, headLen);
            std::string tail = CodepointsToUtf8(cps, cps.size() - tailLen, cps.size());
            std::string finalText = head + newText + tail;
            mPartialTexts[dbSeg.mId] = finalText;
            // 部分替换不标记mReplacedDbIds: 记录未被整条替换, 后续窗口仍可继续替换其余时间区间
            // [部分替换日志] QWen仅覆盖记录中间区间: dbId/双方时间范围/保留头尾长度/替换前后全文,
            // 供审计核对未覆盖区间原文是否被完整保留(替代旧fragment guard直接丢弃QWen文本的行为)
            SLOG_WARN << "QwenTransMerger: QWen partial replace (keep db head/tail), audioId=" << mAudioId
                      << " dbId=" << dbSeg.mId << " qwen=" << qwenSeg.startTime << "-" << qwenSeg.endTime
                      << " db=" << dbSeg.mStartTime << "-" << dbSeg.mEndTime << " keepHeadCps=" << headLen
                      << " keepTailCps=" << tailLen << " old=[" << base << "] slice=[" << newText << "] new=["
                      << finalText << "]";
            qifeng::aas::AasSegment updated = TransToAasSegment(dbSeg);
            updated.text = finalText;
            ReplacedSegment rs {updated, qwenSeg.startTime, qwenSeg.endTime, dbSeg.mId,
                                CalcSegFlag(dbSeg.mSegFlag == 1, finalText), base};
            replaced.push_back(rs);
            return;
        }

        // 碎片防御(仅整条全量替换场景): 新文本远短于DB现有内容(视为窗口截断的残句/碎片)时不替换,
        // 保留DB原文避免丢句; 部分覆盖场景头/尾切片已保留原文, 无丢失风险, 不适用本防御
        size_t oldLen = Utf8ToCodepoints(dbSeg.mContent).size();
        size_t newLen = Utf8ToCodepoints(newText).size();
        if (oldLen > 0 && newLen < oldLen / 3) {
            SLOG_WARN << "QwenTransMerger: skip replace (fragment guard), audioId=" << mAudioId << " dbId=" << dbSeg.mId
                      << " oldLen=" << oldLen << " newLen=" << newLen << " old=[" << dbSeg.mContent << "] new=["
                      << newText << "]";
            return;
        }

        qifeng::aas::AasSegment updated = TransToAasSegment(dbSeg);
        updated.text = newText;
        // 整条替换后累积文本已失效, 清除部分替换累积
        mPartialTexts.erase(dbSeg.mId);
        // 标记已替换, 防止后续QWen窗口(存在约10s重叠)重复替换
        mReplacedDbIds.insert(dbSeg.mId);
        // [替换] qwen=[QWen原始时间段] db=[DB记录时间段] old=[DB原文本] new=[替换后的Qwen3文本]
        SLOG_DEBUG << "QwenTransMerger: QWen text replaced, audioId=" << mAudioId << " qwen=" << qwenSeg.startTime
                   << "-" << qwenSeg.endTime << " db=" << dbSeg.mStartTime << "-" << dbSeg.mEndTime << " old=["
                   << dbSeg.mContent << "] new=[" << newText << "]";
        ReplacedSegment rs {updated,   qwenSeg.startTime,   qwenSeg.endTime,
                            dbSeg.mId, CalcSegFlag(dbSeg.mSegFlag == 1, newText), dbSeg.mContent};
        replaced.push_back(rs);
    }

    // 暂存因窗口截断而未被替换的QWen文本, 拼接至下一窗口开头续接句子
    // 限制长度防止长时间会议中截断片段持续累积
    void QwenTransMerger::AppendPendingQwenTail(const std::string &text) {
        if (text.empty()) {
            return;
        }
        mPendingQwenTail += text;
        auto cps = Utf8ToCodepoints(mPendingQwenTail);
        constexpr size_t kMaxTailCps = 200;
        if (cps.size() > kMaxTailCps) {
            mPendingQwenTail = CodepointsToUtf8(cps, cps.size() - kMaxTailCps, cps.size());
        }
    }

    // [Q缓存] 暂存未匹配到完整DB记录的QWen段:
    // Paraformer落库滞后于QWen窗口(覆盖该窗口的记录尚未写入DB)时, 段先缓存等待,
    // 由ReplayPendingQwenSegs在缺失记录入库后重放完成融合。同范围段去重;
    // 缓存超限时淘汰时间最早的段: 按核心原则先用当前DB尝试重叠覆盖(有重叠用Qwen覆盖
    // paraformer记录), 无重叠则将Qwen额外数据插入DB落库, 防止内存增长且不丢失Qwen文本
    void QwenTransMerger::CachePendingQwenSeg(const qifeng::aas::AasSegment &qwenSeg,
                                              const std::vector<models::Trans> &dbSegs,
                                              std::vector<ReplacedSegment> &replaced) {
        if (qwenSeg.text.empty()) {
            return;
        }
        // 去重: 与已缓存段时间范围完全一致时不再重复缓存
        for (const auto &pq : mPendingQwenSegs) {
            if (pq.seg.startTime == qwenSeg.startTime && pq.seg.endTime == qwenSeg.endTime) {
                return;
            }
        }
        constexpr int MaxRetry = 6;
        constexpr size_t MaxPendingQwenSegs = 100;
        // 缓存超限: 淘汰时间最早的段
        if (mPendingQwenSegs.size() >= MaxPendingQwenSegs) {
            auto it = std::min_element(
                mPendingQwenSegs.begin(), mPendingQwenSegs.end(),
                [](const PendingQwenSeg &a, const PendingQwenSeg &b) { return a.seg.startTime < b.seg.startTime; });
            SLOG_WARN << "QwenTransMerger: pending qwen seg cache overflow, evict oldest, audioId=" << mAudioId
                      << " qwen=" << it->seg.startTime << "-" << it->seg.endTime << " text=[" << it->seg.text << "]";
            // (有重叠则用Qwen原地覆盖paraformer记录, 不新增ID); 无重叠时禁止插入, 丢弃该段并告警,
            // 由stop/Flush对仍缓存的段统一兜底插入, 避免流式中产生重复ID/污染数据。
            SplitAndReplaceTextBySpeaker(it->seg, dbSegs, replaced);
            mPendingQwenSegs.erase(it);
        }
        PendingQwenSeg pq;
        pq.seg = qwenSeg;
        pq.retryLeft = MaxRetry;
        mPendingQwenSegs.push_back(pq);
        // 按时间排序, 保证重放按音频时间顺序处理
        std::sort(mPendingQwenSegs.begin(), mPendingQwenSegs.end(),
                  [](const PendingQwenSeg &a, const PendingQwenSeg &b) { return a.seg.startTime < b.seg.startTime; });
        SLOG_DEBUG << "QwenTransMerger: QWen segment cached (paraformer not landed yet), audioId=" << mAudioId
                   << " qwen=" << qwenSeg.startTime << "-" << qwenSeg.endTime << " speaker=[" << qwenSeg.speakerName
                   << "] text=[" << qwenSeg.text << "] cached=" << mPendingQwenSegs.size();
    }

    // [Q缓存] 重放暂存的QWen段(延后一拍再匹配: 处理上一拍暂存的窗口, 此时paraformer已基本落库):
    // 优先处理数据库中时间戳已明确且记录已入库的记录, 用qwen完全替换paraformer;
    // 缺失记录完成入库后, 缓存段在此与记录融合(替换/暂存续接), 并从缓存移除;
    // 仍未匹配到记录则扣减剩余尝试次数, 尝试耗尽后保留待stop/Flush统一插入, 流式不insert
    void QwenTransMerger::ReplayPendingQwenSegs(const std::vector<models::Trans> &dbSegs,
                                                std::vector<ReplacedSegment> &replaced) {
        if (mPendingQwenSegs.empty()) {
            return;
        }
        std::vector<PendingQwenSeg> next;
        next.reserve(mPendingQwenSegs.size());
        for (auto &pq : mPendingQwenSegs) {
            // 缺失记录已入库 → 按现有匹配逻辑融合; 仍未匹配(空匹配/记录不完整)由内部暂存去重, 不重复入缓存
            bool matched = SplitAndReplaceTextBySpeaker(pq.seg, dbSegs, replaced);
            if (matched) {
                SLOG_INFO << "QwenTransMerger: pending qwen seg merged, audioId=" << mAudioId
                          << " qwen=" << pq.seg.startTime << "-" << pq.seg.endTime << " text=[" << pq.seg.text << "]";
                continue;  // 完成融合(替换或尾部暂存续接), 从缓存移除
            }
            if (--pq.retryLeft <= 0) {
                // 重试耗尽 = 判定paraformer丢数据。但除stop外不insert, 也不丢弃(qwen不能丢失),
                // 置retryLeft=0保留该段, 由stop/Flush统一兜底插入(见FlushPendingQwenSegs)。
                SLOG_WARN << "QwenTransMerger: pending qwen seg retry exhausted, keep for flush (no insert until "
                             "stop), audioId="
                          << mAudioId << " qwen=" << pq.seg.startTime << "-" << pq.seg.endTime << " text=["
                          << pq.seg.text << "]";
                pq.retryLeft = 0;  // 不再重试, 待Flush统一插入
                next.push_back(pq);
                continue;
            }
            SLOG_DEBUG << "QwenTransMerger: pending qwen seg still unmatched, keep cached, audioId=" << mAudioId
                       << " qwen=" << pq.seg.startTime << "-" << pq.seg.endTime << " retryLeft=" << pq.retryLeft;
            next.push_back(pq);
        }
        mPendingQwenSegs.swap(next);
    }

    // [Q缓存] 冲刷全部暂存QWen段(任务停止/最终冲刷时调用):
    // 未匹配到DB记录的缓存段由PushReplacedSegments插入DB新记录, 保证QWen数据一定写入数据库
    void QwenTransMerger::FlushPendingQwenSegs(std::vector<ReplacedSegment> &replaced) {
        for (const auto &pq : mPendingQwenSegs) {
            SLOG_INFO << "QwenTransMerger: flush pending qwen seg, audioId=" << mAudioId << " qwen=" << pq.seg.startTime
                      << "-" << pq.seg.endTime << " text=[" << pq.seg.text << "]";
            ReplacedSegment rs {pq.seg, pq.seg.startTime, pq.seg.endTime};
            replaced.push_back(rs);
        }
        mPendingQwenSegs.clear();
        // 截断暂存文本(记录尾超出窗口时暂存)若仍有残留, 需一并落库避免文本丢失
        if (!mPendingQwenTail.empty()) {
            SLOG_WARN << "QwenTransMerger: flush pending qwen tail, audioId=" << mAudioId << " text=["
                      << mPendingQwenTail << "]";
            if (!replaced.empty() && replaced.back().dbId == 0) {
                // 附着到最后一个待插入的兜底段(未匹配DB, 将插入新记录), 保持文本连续性
                replaced.back().seg.text += mPendingQwenTail;
            } else {
                // 无可附着的兜底段: mPendingQwenTail 均来自"记录尾超出窗口被保留DB原文/续接"时
                // 的QWen残句, 其音频内容已存在于对应已保留的DB记录中, 属冗余文本。
                // 若在此单独默认构造一个AasSegment并插入DB, 会因startTime/endTime/speaker字段
                // 未初始化(garbage)而写入 qwen=0-0 的脏记录, 且造成文本重复, 故直接丢弃。
                SLOG_WARN << "QwenTransMerger: drop redundant pending qwen tail (no attach target), audioId="
                          << mAudioId << " tail=[" << mPendingQwenTail << "]";
            }
            mPendingQwenTail.clear();
        }
    }

    // 任务停止/最终冲刷入口:
    // 0) 置位mStopped: 使此后OnQwenResult(任务停止后在途/晚到的QWen窗口)走stop兜底立即落库
    // 1) 最后重放一次缓存段: stop前Paraformer缺失记录可能刚入库, 能匹配的记录按正常逻辑替换融合落库
    // 2) 剩余未匹配的缓存段兜底: 全部以替换处理(见PushReplacedSegments), 保证结果为qwen内容
    void QwenTransMerger::Flush() {
        std::lock_guard<std::mutex> lock(mMutex);
        mStopped = true;  // 启用stop兜底路径
        std::vector<ReplacedSegment> replaced;

        // 读取当前DB记录(与OnQwenResult一致, 含D缓存合并), 重放缓存段完成最后一次融合
        TransDao dao;
        std::vector<models::Trans> dbSegs;
        auto transList = dao.GetByAudioId(mAccountId, mAudioId);
        for (const auto &t : transList) {
            if (t.mIsDiscard != 0) {
                continue;
            }
            dbSegs.push_back(t);
        }
        for (const auto &pd : mPendingDbSegs) {
            if (mReplacedDbIds.count(pd.mId) != 0) {
                continue;
            }
            bool exists = false;
            for (const auto &t : dbSegs) {
                if (t.mId == pd.mId) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                dbSegs.push_back(pd);
            }
        }
        if (dbSegs.size() > transList.size()) {
            std::sort(dbSegs.begin(), dbSegs.end(),
                      [](const models::Trans &a, const models::Trans &b) { return a.mStartTime < b.mStartTime; });
        }
        ReplayPendingQwenSegs(dbSegs, replaced);

        // 仍未匹配的缓存段兜底(不落库的旧逻辑已移除): PushReplacedSegments会插入DB, 保证文本不丢失
        FlushPendingQwenSegs(replaced);
        if (replaced.empty()) {
            return;
        }
        SLOG_INFO << "QwenTransMerger: flush pending qwen results, audioId=" << mAudioId
                  << " segments=" << replaced.size();
        PushReplacedSegments(replaced);
    }

    // [D缓存] 更新未匹配DB记录缓存(D结果, 对应算法题中vector<Info> D):
    // 收集本次Qwen窗口内未被任何QWen段替换的DB记录(如记录尾超出窗口、Paraformer刚落库等),
    // 供下次Qwen窗口合并参与匹配, 防止Paraformer滚动更新(删旧插新)导致记录遗漏或替换效果丢失。
    // 同时淘汰已替换/已过期的缓存记录, 并限制缓存上限防止长时间会议内存增长。
    void QwenTransMerger::UpdatePendingDbSegs(const std::vector<models::Trans> &dbSegs, int64_t windowStart,
                                              int64_t windowEnd) {
        std::vector<models::Trans> next;
        next.reserve(mPendingDbSegs.size() + dbSegs.size());
        // 1) 保留上次缓存中仍未处理且时间不落本次窗口的记录(等待更晚的窗口)
        for (const auto &pd : mPendingDbSegs) {
            if (mReplacedDbIds.count(pd.mId) != 0) {
                continue;  // 已被替换/删除, 移除
            }
            if (!(pd.mStartTime < windowEnd && windowStart < pd.mEndTime)) {
                next.push_back(pd);
            }
        }
        // 2) 收集本次窗口内未被替换的记录(D): 时间与窗口重叠且未被任何QWen段消耗
        for (const auto &t : dbSegs) {
            if (mReplacedDbIds.count(t.mId) != 0) {
                continue;
            }
            if (t.mStartTime < windowEnd && windowStart < t.mEndTime) {
                next.push_back(t);
            }
        }
        // 3) 按时间排序 + 数量上限淘汰最旧
        std::sort(next.begin(), next.end(),
                  [](const models::Trans &a, const models::Trans &b) { return a.mStartTime < b.mStartTime; });
        constexpr size_t kMaxPendingDbSegs = 200;
        if (next.size() > kMaxPendingDbSegs) {
            next.erase(next.begin(), next.begin() + static_cast<ptrdiff_t>(next.size() - kMaxPendingDbSegs));
        }
        if (next.size() != mPendingDbSegs.size()) {
            SLOG_DEBUG << "QwenTransMerger: update pending db segs (D cache), audioId=" << mAudioId
                       << " pending=" << next.size() << " window=" << windowStart << "-" << windowEnd;
        }
        mPendingDbSegs.swap(next);
    }

    // 推送替换后的分段到前端, 并同步更新DB文本内容(保持DB ID不变)
    // dbId在匹配阶段已确定, 此处直接按ID更新, 无需再匹配DB记录
    // 支持四种情况: 1)deleted=融合删除的记录(删DB+推空文本) 2)普通替换(更新文本+时间)
    //              3)dbId==0的兜底段(Flush/重试耗尽/缓存溢出): 插入DB新记录, 保证QWen数据一定落库
    void QwenTransMerger::PushReplacedSegments(std::vector<ReplacedSegment> &segments) {
        TransDao dao;
        for (auto &rs : segments) {
            const auto &seg = rs.seg;
            if (rs.deleted && rs.dbId != 0) {
                // [删除日志] 被融合删除的DB记录: 删除DB记录 + 推送空文本给前端移除展示
                //   dbId=被删除记录ID qwen=触发删除的QWen段时间范围
                //   db=被删记录的时间范围 old=[被删记录原文(审计留档)]
                // 注意: 该记录此后在DB中不可再查, 原文仅存在于本日志
                SLOG_DEBUG << "QwenTransMerger: DB record deleted, audioId=" << mAudioId << " dbId=" << rs.dbId
                           << " qwen=" << rs.qwenStartTime << "-" << rs.qwenEndTime << " db=" << seg.startTime << "-"
                           << seg.endTime << " old=[" << rs.oldContent << "]";
                dao.DeleteById(mAccountId, rs.dbId);
            } else if (rs.dbId != 0) {
                // [替换落库日志] 更新DB记录文本与时间(QWen融合/切分可能调整时间范围)
                //   dbId=被更新记录ID segFlag=分段标记(1=新段落开始)
                //   qwen=QWen原始段时间范围 db=落库后的时间范围
                //   old=[更新前原文] new=[更新后QWen文本]
                // db范围是否覆盖整个qwen范围, 是判断时间戳融合是否完整的关键依据
                models::Trans t;
                t.mId = rs.dbId;
                t.mAccountId = mAccountId;
                t.mStartTime = static_cast<int32_t>(seg.startTime);
                t.mEndTime = static_cast<int32_t>(seg.endTime);
                t.mContent = seg.text;
                t.mSegFlag = rs.segFlag ? 1 : 0;
                t.mSpeakerName = seg.speakerName;
                // [替换] DB 记录更新: qwen=[QWen原始时间段] db=[DB记录时间段] old=[替换前内容] new=[替换后Qwen3文本]
                SLOG_DEBUG << "QwenTransMerger: DB text updated, audioId=" << mAudioId << " dbId=" << rs.dbId
                           << " segFlag=" << rs.segFlag << " qwen=" << rs.qwenStartTime << "-" << rs.qwenEndTime
                           << " db=" << seg.startTime << "-" << seg.endTime << " old=[" << rs.oldContent << "] new=["
                           << seg.text << "]";
                dao.UpdateContent(t);
            } else {
                // [局部兜底: 永不insert] 未匹配到任何paraformer记录(该qwen区间无paraformer记录支撑)的段,
                // 按"替换方式,永不insert"原则不落库: 跳过DB写入, 也不推送前端(无稳定dbId)。
                // 目的: DB最终仅保留被qwen替换后的paraformer记录, 结果内容全部为qwen转写。
                SLOG_WARN << "QwenTransMerger: drop unmatched qwen seg (no paraformer record, no insert), audioId="
                          << mAudioId << " qwen=" << rs.qwenStartTime << "-" << rs.qwenEndTime << " seg="
                          << seg.startTime << "-" << seg.endTime << " text=[" << seg.text << "]";
                continue;
            }

            std::string json = BuildReplacedSegmentMessage(mAudioId, seg, rs.dbId, rs.segFlag);
            auto &dispatchMgr = RealtimeDispatchManager::GetInstance();
            bool ok = dispatchMgr.Distribute(json, EClientType::kTranscribe);
            if (!ok) {
                SLOG_DEBUG << "QwenTransMerger: no ws clients for QWen replaced segment, audioId=" << mAudioId;
            }
        }
        SLOG_INFO << "QwenTransMerger: QWen replaced segments pushed, audioId=" << mAudioId
                  << " count=" << segments.size();
    }

}  // namespace qifeng_ca
