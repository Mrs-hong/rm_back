/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <map>

#include "spdlog/fmt/bundled/args.h"
#include "spdlog/fmt/bundled/core.h"
#include "spdlog/fmt/bundled/format.h"

#include "common/logger.h"

#include "lms/metting/private_include/llm_ability.h"
#include "lms/metting/private_include/prompt_data.h"
#include "lms/metting/private_include/text_utils.h"

namespace qifeng {

    namespace lms {
        static std::string InvokeWithPrompt(std::shared_ptr<ModelContext> ctx, std::string_view systemPrompt,
                                            std::string_view content, bool enableThinking) {
            ModelRequest request;
            request.systemPrompt = std::string {systemPrompt};
            request.userPrompt = std::string {content};
            // 关闭思考模式
            request.enableThinking = 0;
            if (request.enableThinking) {
                request.configJson = R"({"repetition_penalty":1.1,"temperature":1.0,"top_p":0.95,"top_k":20})";
            } else {
                request.configJson = R"({"repetition_penalty":1.1,"temperature":0.7,"top_p":0.8,"top_k":20})";
            }

            std::string result = ctx->Generate(request);
            SLOG_DEBUG << "InvokeWithPrompt enableThinking:=" << request.enableThinking;
            SLOG_TRACE << "systemPrompt systemPrompt:=" << request.systemPrompt;
            SLOG_TRACE << "userPrompt userPrompt:=" << request.userPrompt;
            SLOG_TRACE << "[InvokeWithPrompt] Result = " << result;
            return StripTags(result);
        }

        // 将 KindSwitch 原始输出映射为 MeetingType 枚举
        static MeetingType ParseMeetingType(std::string_view raw) {
            if (raw.find("morning") != std::string_view::npos) {
                return MeetingType::Morning;
            }
            if (raw.find("decision") != std::string_view::npos) {
                return MeetingType::Decision;
            }
            if (raw.find("CPC") != std::string_view::npos) {
                return MeetingType::Cpc;
            }
            if (raw.find("invalid") != std::string_view::npos) {
                return MeetingType::Invalid;
            }
            // "review" / "default" / 空字符串均降级为 General
            return MeetingType::General;
        }

        std::chrono::milliseconds EstimateSummarizeTime(std::size_t textLength, SummarizeMode mode,
                                                        std::size_t chunkSize, std::size_t docCount) {
            // ── 速度常量（基于 BMRuntime 实测：decodeSpeed≈13.4 token/s，保守取 11.0 兜住极端场景） ──
            constexpr double kGenSpeed = 11.0;        // tokens/sec，生成速度（保守值）
            constexpr double kInputSpeed = 500.0;     // tokens/sec，prompt 处理速度
            constexpr double kOverhead = 0.5;         // 每次 LLM 调用固定开销（秒）
            constexpr std::size_t kMaxInput = 15000;  // llm.cpp 输入截断上限

            auto callCost = [=](std::size_t inputChars, std::size_t outputChars) -> double {
                return kOverhead + std::min(inputChars, kMaxInput) / kInputSpeed + outputChars / kGenSpeed;
            };

            // ── 信息密度：文本越长，每块提取的信息占比越低 ──
            double density = std::max(0.15, 0.6 / (1.0 + static_cast<double>(textLength) / 15000.0));
            auto N = docCount;
            double t = 0.0;
            double tSec = 0.0;

            if (mode == SummarizeMode::Detailed) {
                // Refine 增量式：first/mid 输出与分块大小正相关（无硬 floor，避免掩码文本长度差异）
                auto firstOut = static_cast<std::size_t>(2500 + chunkSize * 2);
                auto midOut = static_cast<std::size_t>(400 + chunkSize * 0.4);
                t = callCost(chunkSize, firstOut);
                if (N > 2) {
                    t += callCost(chunkSize, midOut) * (N - 2);
                }
                t += callCost(chunkSize, midOut);  // last
                t += callCost(midOut, 30);         // keywords
                tSec = t * 1.10;
            } else if (mode == SummarizeMode::Simple) {
                std::size_t mapOut = static_cast<std::size_t>(chunkSize * density);
                t = static_cast<double>(N) * callCost(chunkSize, mapOut);
                tSec = t * 1.10;
            } else {
                // Basic 模式
                if (textLength > 7500) {
                    // 长文本：kind + N*map + topic + overview + keywords（含 thinking 开销，保守估计）
                    t = callCost(chunkSize, 10);  // kind
                    std::size_t mapOutPer = static_cast<std::size_t>(chunkSize * density);
                    t += static_cast<double>(N) * callCost(chunkSize, mapOutPer);  // map × N
                    t += callCost(chunkSize, 100);                                 // topic
                    std::size_t combined = std::min(mapOutPer * N, std::size_t {11264});
                    std::size_t overviewOut = static_cast<std::size_t>(combined * 0.5);
                    t += callCost(combined, overviewOut);  // overview
                    t += callCost(overviewOut, 50);        // keywords
                    tSec = t * 1.75;                       // 长文本 thinking 开销大，保守乘数
                } else if (textLength >= 2500) {
                    // 中文本：kind + topic + main（thinking 模式输出 token 可达 2x，保守估计）
                    std::size_t mainOut =
                        std::min(static_cast<std::size_t>(textLength * density * 1.5), std::size_t {2000});
                    t = callCost(textLength, 10);        // kind
                    t += callCost(textLength, 100);      // topic
                    t += callCost(textLength, mainOut);  // main
                    t += callCost(mainOut, 30);          // keywords
                    tSec = t * 1.75;                     // 兜住极端场景
                } else {
                    // 短文本：simple_once
                    std::size_t mainOut = static_cast<std::size_t>(textLength * density * 1.2);
                    t = callCost(textLength, mainOut);
                    t += callCost(mainOut, 30);
                    tSec = t * 1.75;
                }
            }

            return std::chrono::milliseconds {static_cast<long long>(tSec * 1000)};
        }

        std::chrono::milliseconds EstimateCallCost(std::size_t inputChars, std::size_t outputChars) {
            constexpr double kGenSpeed = 11.0;
            constexpr double kInputSpeed = 500.0;
            constexpr double kOverhead = 0.5;
            constexpr std::size_t kMaxInput = 15000;
            double t = kOverhead + std::min(inputChars, kMaxInput) / kInputSpeed + outputChars / kGenSpeed;
            return std::chrono::milliseconds {static_cast<long long>(t * 1000)};
        }

        double EstimateDensity(std::size_t textLength) {
            return std::max(0.15, 0.6 / (1.0 + static_cast<double>(textLength) / 15000.0));
        }

        // ── 会议分类 ──
        MeetingType ClassifyMeeting(std::shared_ptr<ModelContext> ctx, std::string_view content) {
            ctx->Reset();
            auto raw = InvokeWithPrompt(ctx, detail::KindSwitchInvalid, content, false);
            return ParseMeetingType(raw);
        }

        // ── 议题提取 ──
        std::string ExtractTopic(std::shared_ptr<ModelContext> ctx, std::string_view content) {
            ctx->Reset();
            auto raw = InvokeWithPrompt(ctx, detail::Topic, content, false);
            return Strip(raw);
        }

        // ── 纪要案例写作风格提取 ──
        std::string ExtractStyle(std::shared_ptr<ModelContext> ctx, std::string_view content) {
            ctx->Reset();
            return InvokeWithPrompt(ctx, detail::ExampleStyleExtract, content, true);
        }

        // ── 关键词提取 ──
        std::vector<std::string> ExtractKeywords(std::shared_ptr<ModelContext> ctx, std::string_view content) {
            ctx->Reset();
            auto raw = InvokeWithPrompt(ctx, detail::KeyExtract, content, false);
            return ParseKeywords(raw);
        }

        // ---- Map 阶段（按会议类型的片段级信息提取）----
        std::string Map(std::shared_ptr<ModelContext> ctx, std::string_view content, std::string_view topic) {
            ctx->Reset();
            auto tmpUserPrompt = ReplacePlaceholders(detail::GetMettingInfo_A, {{"meeting_topic", topic}});
            auto userPrompt = fmt::format("{}\n{}", tmpUserPrompt, content);

            return InvokeWithPrompt(ctx, detail::SystemPrompt, userPrompt, true);
        }

        // ---- Overview 阶段（全局整合）----
        std::string OverviewGeneral(std::shared_ptr<ModelContext> ctx, std::string_view content,
                                    const SummaryHints& hints) {
            ctx->Reset();
            std::string tmpUserPrompt = "";
            if (!hints.exampleStyle.empty()) {
                tmpUserPrompt =
                    ReplacePlaceholders(detail::OutByMettingInfo_A_EXAMPLE, {{"meeting_topic", hints.topic},
                                                                             {"template", detail::OutputGeneral},
                                                                             {"example_style", hints.exampleStyle}});
            } else {
                tmpUserPrompt = ReplacePlaceholders(
                    detail::OutByMettingInfo_A, {{"meeting_topic", hints.topic}, {"template", detail::OutputGeneral}});
            }

            auto userPrompt = fmt::format("{}\n{}", tmpUserPrompt, content);

            return InvokeWithPrompt(ctx, detail::SystemPrompt, userPrompt, true);
        }

        // ---- 一次性总结 ----
        std::string OnceSimpleSummary(std::shared_ptr<ModelContext> ctx, std::string_view content) {
            ctx->Reset();
            std::string tmpUserPrompt =
                ReplacePlaceholders(detail::OnceSummaryShort_A, {{"template", detail::OutputGeneral}});
            std::string userPrompt = fmt::format("{}\n{}", tmpUserPrompt, content);
            return InvokeWithPrompt(ctx, detail::SystemPrompt, userPrompt, false);
        }

        std::string OnceSummaryGeneral(std::shared_ptr<ModelContext> ctx, std::string_view content,
                                       const SummaryHints& hints) {
            ctx->Reset();
            std::string tmpUserPrompt = "";
            if (!hints.exampleStyle.empty()) {
                tmpUserPrompt =
                    ReplacePlaceholders(detail::OnceSummaryMedium_A_EXAMPLE, {{"meeting_topic", hints.topic},
                                                                              {"template", detail::OutputGeneral},
                                                                              {"example_style", hints.exampleStyle}});
            } else {
                tmpUserPrompt = ReplacePlaceholders(
                    detail::OnceSummaryMedium_A, {{"meeting_topic", hints.topic}, {"template", detail::OutputGeneral}});
            }

            auto userPrompt = fmt::format("{}\n{}", tmpUserPrompt, content);
            // 完整模式固定启用思维链
            return InvokeWithPrompt(ctx, detail::SystemPrompt, userPrompt, true);
        }

        // ---- 增量式总结 ----
        std::string RefineFirst(std::shared_ptr<ModelContext> ctx, std::string_view content) {
            // 完整模式固定启用思维链
            ctx->Reset();
            auto prompt = ReplacePlaceholders(detail::RefineFirst, {{"meeting_topic", ""}, {"example_style", ""}});
            return InvokeWithPrompt(ctx, prompt, content, true);
        }

        std::string RefineMiddle(std::shared_ptr<ModelContext> ctx, std::string_view content,
                                 const std::string& previousSummary) {
            ctx->Reset();
            auto invokeTemplate = detail::RefineMiddle;

            auto systemPrompt = ReplacePlaceholders(
                invokeTemplate, {{"meeting_topic", ""}, {"example_style", ""}, {"previous_summary", previousSummary}});

            // 完整模式固定启用思维链
            return InvokeWithPrompt(ctx, systemPrompt, content, true);
        }

        std::string RefineLast(std::shared_ptr<ModelContext> ctx, std::string_view content,
                               const std::string& previousSummary) {
            ctx->Reset();
            auto invokeTemplate = detail::RefineLast;
            auto outputTemplate = std::string {detail::OutputGeneral};

            auto systemPrompt = ReplacePlaceholders(invokeTemplate, {{"meeting_topic", ""},
                                                                     {"example_style", ""},
                                                                     {"previous_summary", previousSummary},
                                                                     {"template", outputTemplate}});

            // 完整模式固定启用思维链
            return InvokeWithPrompt(ctx, systemPrompt, content, true);
        }

    }  // namespace lms

}  // namespace qifeng
