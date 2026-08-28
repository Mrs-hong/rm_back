/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
// #include <algorithm>

#include "common/logger.h"

#include "lms/metting/private_include/llm_ability.h"
#include "lms/metting/private_include/normal_summarizer.h"
#include "lms/metting/private_include/text_utils.h"

namespace qifeng {

    namespace lms {

        // 三档阈值划分总结策略：Short(<2500) / Medium(2500~7500) / Long(>7500)
        static constexpr size_t kShortThreshold = 2500;
        static constexpr size_t kLongThreshold = 7500;

        // 确保生成的关键词数量为 5 个：不足时重试，超出时截断
        static std::vector<std::string> EnsureFiveKeywords(std::shared_ptr<ModelContext> ctx,
                                                           std::string_view overview) {
            auto keywords = ExtractKeywords(ctx, overview);
            SLOG_DEBUG << "ExtractKeywords returned " << keywords.size() << " keywords";

            if (keywords.size() < 5) {
                SLOG_DEBUG << "ExtractKeywords returned only " << keywords.size() << " keywords (< 5), retrying...";
                keywords = ExtractKeywords(ctx, overview);
                SLOG_DEBUG << "Retry ExtractKeywords returned " << keywords.size() << " keywords";
            }

            if (keywords.size() > 5) {
                SLOG_DEBUG << "ExtractKeywords returned " << keywords.size() << " keywords, truncating to 5";
                keywords.resize(5);
            }

            return keywords;
        }

        // 将会议基本信息字段插入到 overview 中 "## 会议纪要" 之后
        // info 字段为空时插入 "待补充"，否则插入用户提供的值
        static void ReplaceMettingInfoFields(std::string& overview, const NormalMettingInfo& info) {
            auto pos = overview.find("会议纪要");
            if (pos == std::string::npos) {
                return;
            }
            auto lineEnd = overview.find('\n', pos);
            if (lineEnd == std::string::npos) {
                lineEnd = overview.size() - 1;
            }

            auto field = [](const std::string& label, const std::string& value) {
                return "- **" + label + "**：" + (value.empty() ? "[请填写]" : value) + "\n";
            };

            std::string infoBlock = "\n### 会议基本信息\n";
            infoBlock += field("会议时间", info.date);
            infoBlock += field("会议地点", info.location);
            infoBlock += field("主持人", info.host);
            infoBlock += field("参会人员", info.attendees);
            infoBlock += "\n---\n";
            overview.insert(lineEnd + 1, infoBlock);
        }

        // 将关键词拼接到 overview 中 "会议纪要" 之后
        static void SpliceKeywords(std::string& ov, const std::vector<std::string>& kw) {
            const std::string target = "会议纪要";
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

        // 统计 topic 字符串中的议题数量（匹配 "1.xxx" "2.xxx" 等数字编号开头的行）
        static int CountTopicItems(const std::string& topic) {
            int count = 0;
            bool atLineStart = true;
            for (size_t i = 0; i < topic.size(); ++i) {
                if (atLineStart && topic[i] >= '0' && topic[i] <= '9') {
                    size_t j = i;
                    while (j < topic.size() && topic[j] >= '0' && topic[j] <= '9')
                        j++;
                    if (j < topic.size() &&
                        (topic[j] == '.' || (j + 2 < topic.size() && topic.compare(j, 3, "、") == 0))) {
                        count++;
                    }
                    atLineStart = false;
                }
                if (topic[i] == '\n') {
                    atLineStart = true;
                }
            }
            return count;
        }

        NormalMettingSummarizer::NormalMettingSummarizer(std::shared_ptr<Model> model,
                                                         const NormalMettingInfo& mettingInfo,
                                                         const MettingHints& hints)
            : mModel {model}, mModelContext {model->CreateContext()}, mRunning {false},
              mMettingInfo {mettingInfo}, mHints {hints} {
        }

        SummaryResult
        NormalMettingSummarizer::SynchronouslyExecute(std::function<void(ProgressStat)> progressObserver) {
            try {
                {
                    // CAS 防止并发重入
                    bool expected {false};
                    if (!mRunning.compare_exchange_strong(expected, true)) {
                        return SummaryError {SummaryError::StateCode::SUMMARY_ERROR, "already running"};
                    }
                }

                auto result = Execute(progressObserver);

                mRunning = false;
                mExecutionCV.notify_all();
                return result;
            } catch (const ModelContext::CanceledException& e) {
                mRunning = false;
                mExecutionCV.notify_all();

                SLOG_ERROR << "NormalMettingSummarizer interrupted: " << e.what();
                return SummaryError {SummaryError::StateCode::INTERRUPTED, e.what()};
            } catch (const std::exception& e) {
                mRunning = false;
                mExecutionCV.notify_all();

                SLOG_ERROR << "NormalMettingSummarizer exception: " << e.what();
                return SummaryError {SummaryError::StateCode::INFERENCE_ERROR, e.what()};
            } catch (...) {
                mRunning = false;
                mExecutionCV.notify_all();

                SLOG_ERROR << "NormalMettingSummarizer unknown exception";
                return SummaryError {SummaryError::StateCode::INFERENCE_ERROR, ""};
            }
        }

        SummaryResult NormalMettingSummarizer::Execute(std::function<void(ProgressStat)> progressObserver) {
            uint32_t step = 0;
            auto begin = std::chrono::system_clock::now();
            auto reportProgress = [&progressObserver, &step, &begin](std::chrono::milliseconds estimateRemainingTime) {
                std::chrono::milliseconds spentTime =
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - begin);
                SLOG_DEBUG << "NormalSummarizer execute " << step << " step, spentTime = " << spentTime.count()
                           << " ms, estimateRemainingTime = " << estimateRemainingTime.count() << " ms";
                step++;

                if (progressObserver) {
                    progressObserver(ProgressStat {spentTime, estimateRemainingTime});
                }
            };

            const auto& mettingInfo = mMettingInfo;
            if (auto err = mettingInfo.Validate(); err) {
                SLOG_ERROR << "NormalMettingInfo is invalid, code = " << static_cast<int>(err->code)
                           << ", reason = " << err->reason;
                return err.value();
            }

            // std::string baseMettingInfo = "这是用户输入的会议基本信息：\n" + mettingInfo.FormattedBaseInfo();
            // std::string text = fmt::format("{}\n{}", baseMettingInfo, mettingInfo.text);
            SummaryResult result;
            auto rawTextSize = Utf8Length(mettingInfo.text);
            SLOG_DEBUG << "[NormalMettingSummarizer] start, chars: " << rawTextSize;
            if (rawTextSize < kShortThreshold) {
                reportProgress(EstimateSummarizeTime(rawTextSize, SummarizeMode::Basic, rawTextSize, 1));
                result = SummarizeShort(mettingInfo.text, reportProgress);
            } else {
                auto chunkSize = ComputeChunkSize(mettingInfo.text);
                auto docCount = (rawTextSize + chunkSize - 1) / chunkSize;
                if (docCount < 1)
                    docCount = 1;
                reportProgress(EstimateSummarizeTime(rawTextSize, SummarizeMode::Basic, chunkSize, docCount));
                auto abilityHints = BuildAbilityHints(reportProgress);
                if (rawTextSize <= kLongThreshold) {
                    result = SummarizeMedium(mettingInfo.text, reportProgress, abilityHints);
                } else {
                    result = SummarizeLong(mettingInfo.text, reportProgress, abilityHints);
                }
            }

            // 后处理：将 overview 中会议基本信息字段替换为用户注入的值
            if (std::holds_alternative<SummaryData>(result)) {
                auto& data = std::get<SummaryData>(result);
                ReplaceMettingInfoFields(data.overview, mMettingInfo);
                // data.overview = FixMarkdownSpacing(data.overview);
            }

            reportProgress(std::chrono::milliseconds {0});
            return result;
        }

        bool NormalMettingSummarizer::Running() {
            return mRunning;
        }

        void NormalMettingSummarizer::Stop() {
            if (mRunning) {
                SLOG_INFO << "NormalMettingSummarizer stopped by user";
                mModelContext->CancelGeneration();
            }
        }

        SummaryResult NormalMettingSummarizer::Restart(std::function<void(ProgressStat)> progressObserver) {
            // 1. 取消当前正在执行的任务
            Stop();

            // 2. 等待当前 SynchronouslyExecute 完成
            {
                std::unique_lock<std::mutex> lock(mExecutionMutex);
                mExecutionCV.wait(lock, [this] { return !mRunning; });
            }

            // 3. 重置模型上下文，清除可能残留的异常状态
            mModelContext->Reset();

            // 4. 重新开始总结
            SLOG_INFO << "Summarizer restarting";
            return SynchronouslyExecute(progressObserver);
        }

        SummaryHints NormalMettingSummarizer::BuildAbilityHints(
            const std::function<void(std::chrono::milliseconds)>& reportProgress) {
            SummaryHints abilityHints;
            abilityHints.topic = mHints.topic;

            if (mHints.exampleSummary.empty()) {
                return abilityHints;
            }
            abilityHints.exampleStyle = ExtractStyle(mModelContext, mHints.exampleSummary);
            return abilityHints;
        }
        /*
         * 短文本总结策略：
         * 1. 生成总结
         * 2. 提取关键词
         */
        SummaryResult
        NormalMettingSummarizer::SummarizeShort(const std::string& text,
                                                const std::function<void(std::chrono::milliseconds)>& reportProgress) {
            SLOG_INFO << "===[SummarizeShort] start===";

            // 极短文本走精简模板，不分类不提取议题
            auto overview = OnceSimpleSummary(mModelContext, text);
            auto keywords = EnsureFiveKeywords(mModelContext, overview);

            SpliceKeywords(overview, keywords);

            return SummaryData {overview, keywords};
        }

        /*
         * 中长文本总结策略：
         * 1. 先分类会议类型（晨会 / 会中）
         * 2. 根据会议类型确定议题（用户指定 > 晨会固定模板 > LLM 提取）
         * 3. 生成总结（考虑用户提示）
         * 4. 提取关键词（考虑用户提示）
         */
        SummaryResult
        NormalMettingSummarizer::SummarizeMedium(const std::string& text,
                                                 const std::function<void(std::chrono::milliseconds)>& reportProgress,
                                                 const SummaryHints& abilityHints) {
            SLOG_INFO << "===[SummarizeMedium] start===";
            std::string topic = "";
            if (!abilityHints.topic.empty()) {
                SLOG_INFO << "[SummarizeMedium] use user topic: " << abilityHints.topic;
                topic = abilityHints.topic;
            } else {
                // 取前 kShortThreshold 个字符用于分类
                auto classifyText = Utf8Substring(text, kShortThreshold);
                auto meetingType = ClassifyMeeting(mModelContext, classifyText);
                SLOG_INFO << "[SummarizeMedium] ClassifyMeeting result: " << static_cast<int>(meetingType);
                if (meetingType == MeetingType::Morning) {
                    topic = "1.昨日工作进展同步\n2.今日工作计划\n3.问题讨论";
                } else {
                    topic = ExtractTopic(mModelContext, text);
                    // 后处理：议题数超过 4 个则重新提取，直到满足要求
                    while (CountTopicItems(topic) > 4) {
                        SLOG_INFO << "[SummarizeMedium] topic items > 4, retrying...";
                        topic = ExtractTopic(mModelContext, text);
                    }
                }
            }

            // 重新估算剩余步骤：Topic + OnceSummary + Keywords（中文本）
            {
                auto density = EstimateDensity(Utf8Length(text));
                auto onceOut = std::min(static_cast<std::size_t>(Utf8Length(text) * density * 1.5), std::size_t {1000});
                auto remaining = EstimateCallCost(Utf8Length(text), 100)        // topic
                                 + EstimateCallCost(Utf8Length(text), onceOut)  // once summary
                                 + EstimateCallCost(onceOut, 50);               // keywords
                reportProgress(remaining);
            }
            SLOG_DEBUG << "[SummarizeMedium] topic=" << topic;
            SummaryHints finalHints {topic, abilityHints.exampleStyle};
            auto overview = OnceSummaryGeneral(mModelContext, text, finalHints);
            auto keywords = EnsureFiveKeywords(mModelContext, overview);

            SpliceKeywords(overview, keywords);

            return SummaryData {overview, keywords};
        }
        /*
         * 长文本总结策略：
         * 1. 先判断会议类型
         * 2. map-reduce
         * 2. 根据会议类型确定议题（用户指定 > 晨会固定模板 > LLM 提取）
         * 3. 生成总结（考虑用户提示）
         * 4. 提取关键词（考虑用户提示）
         */
        SummaryResult
        NormalMettingSummarizer::SummarizeLong(const std::string& text,
                                               const std::function<void(std::chrono::milliseconds)>& reportProgress,
                                               const SummaryHints& abilityHints) {
            SLOG_INFO << "===[SummarizeLong] start===";

            auto chunkSize = ComputeChunkSize(text);
            auto chunkOverlap = std::max(1, static_cast<int>(chunkSize / 100));
            // 合并后超过此长度会被截断，防止 Overview 阶段输入过长
            static constexpr size_t MaxMergedLen = 11264;
            auto chunks = SplitText(text, static_cast<int>(chunkSize), static_cast<int>(chunkOverlap));
            if (chunks.empty()) {
                throw std::runtime_error {"Chunks can not be empty."};
            }

            SLOG_INFO << "[SummarizeLong]  chars: " << Utf8Length(text) << ", chunks num: " << chunks.size()
                      << ", chunkSize: " << chunkSize << ", chunkOverlap: " << chunkOverlap;

            std::string topic = "";
            if (!abilityHints.topic.empty()) {
                SLOG_INFO << "[SummarizeLong] use user topic: " << abilityHints.topic;
                topic = abilityHints.topic;
            } else {
                // 取前 kShortThreshold 个字符用于分类
                auto classifyText = Utf8Substring(text, kShortThreshold);
                auto meetingType = ClassifyMeeting(mModelContext, classifyText);
                SLOG_INFO << "[SummarizeLong] ClassifyMeeting result: " << static_cast<int>(meetingType);
                if (meetingType == MeetingType::Morning) {
                    topic = "1.昨日工作进展同步\n2.今日工作计划\n3.问题讨论";
                } else {
                    // 构建议题提取文本：首块全文 + 其他块各取前10%（对齐 Python _build_topic_extraction_text）
                    std::string topicExtractionText = "【第1块全文】\n" + chunks.front();
                    for (size_t i = 1; i < chunks.size(); ++i) {
                        auto cutoff = std::max(size_t {1}, static_cast<size_t>(Utf8Length(chunks[i]) * 0.1));
                        topicExtractionText +=
                            "\n\n【第" + std::to_string(i + 1) + "块前10%】\n" + Utf8Substring(chunks[i], cutoff);
                    }
                    topic = ExtractTopic(mModelContext, topicExtractionText);
                    // 后处理：议题数超过 4 个则重新提取，直到满足要求
                    while (CountTopicItems(topic) > 4) {
                        SLOG_INFO << "[SummarizeLong] topic items > 4, retrying...";
                        topic = ExtractTopic(mModelContext, topicExtractionText);
                    }
                }
            }
            SLOG_DEBUG << "[SummarizeLong] topic: " << topic;
            // Map 阶段：逐块提取关键信息（对齐 Python _map，均以字符数计量）
            size_t maxChunkLenPerDoc = MaxMergedLen / chunks.size();  // 每个 chunk 的等额预算（字符数）
            long long leftBuff =
                static_cast<long long>(maxChunkLenPerDoc * chunks.size()) - 256;  // 留 256 给 system_prompt
            size_t beyondChunkNum = 0;                                            // 超出预算的 chunk 数

            std::vector<std::string> mapOutputs;
            mapOutputs.reserve(chunks.size());
            for (size_t i = 0; i < chunks.size(); ++i) {
                auto mapped = Map(mModelContext, chunks[i], topic);

                SLOG_INFO << "[SummarizeLong] Map chunk[" << i << "] done! chars: " << Utf8Length(mapped);

                // 统计超长块 & 更新剩余预算（按字符数）
                if (Utf8Length(mapped) > maxChunkLenPerDoc) {
                    beyondChunkNum++;
                }
                leftBuff -= static_cast<long long>(Utf8Length(mapped));
                mapOutputs.push_back(std::move(mapped));
            }

            // 超限截断：将溢出量均摊到超长块（按字符数截断）
            if (leftBuff < 0) {
                long long truncateLen = -leftBuff;
                long long perBlockCut = truncateLen / static_cast<long long>(std::max(beyondChunkNum, size_t {1}));
                SLOG_WARN << "Total exceeds map stage limit " << MaxMergedLen << ", excess: " << truncateLen
                          << " chars, perBlockCut: " << perBlockCut << ", beyondChunkNum: " << beyondChunkNum;

                for (size_t i = 0; i < mapOutputs.size(); ++i) {
                    if (Utf8Length(mapOutputs[i]) > maxChunkLenPerDoc) {
                        long long targetLen = static_cast<long long>(Utf8Length(mapOutputs[i])) - perBlockCut;
                        if (targetLen > 0) {
                            SLOG_DEBUG << "Truncating mapOutputs[" << i << "] from " << Utf8Length(mapOutputs[i])
                                       << " to " << targetLen << " chars";
                            mapOutputs[i] = Utf8Substring(mapOutputs[i], static_cast<size_t>(targetLen));
                        }
                    }
                }
            }

            // 合并（对齐 Python: "".join(summaries_info)，无分隔符）
            std::string merged;
            for (size_t i = 0; i < mapOutputs.size(); ++i) {
                merged += mapOutputs[i];
            }

            // 合并后安全截断,兜底保障（按字符数）
            if (Utf8Length(merged) > MaxMergedLen) {
                SLOG_WARN << "Merged text still exceeds limit after map truncation, safety truncating chars: "
                          << Utf8Length(merged) << " -> " << MaxMergedLen;
                merged = Utf8Substring(merged, MaxMergedLen);
            }

            // 重新估算：Overview + Keywords
            {
                auto density = EstimateDensity(Utf8Length(text));
                auto mapOutPer = static_cast<std::size_t>(chunkSize * density);
                auto numChunks = chunks.size();
                auto combined = std::min(mapOutPer * numChunks, std::size_t {11264});
                auto overviewOut = static_cast<std::size_t>(combined * 0.5);
                auto remaining = EstimateCallCost(combined, overviewOut)  // overview
                                 + EstimateCallCost(overviewOut, 50);     // keywords
                reportProgress(remaining);
            }

            // 信息密度过高时追加简化指令
            if (Utf8Length(merged) > 8192) {
                SLOG_INFO << "[SummarizeLong] 合并信息密度过大（> 8192 字），启用简化输出指令";
                merged += "该会议内容信息密度过大，详略得当地总结该会议";
            }

            // Overview 阶段：全类型统一走带 hints 的重载（启用思维链）
            SummaryHints finalHints {topic, abilityHints.exampleStyle};
            std::string overviewContent =
                "根据提取出的会议信息，帮我生成一篇高质量会议纪要，提取出信息如下：\n" + merged;

            auto overview = OverviewGeneral(mModelContext, overviewContent, finalHints);

            auto keywords = EnsureFiveKeywords(mModelContext, overview);

            SpliceKeywords(overview, keywords);
            return SummaryData {overview, keywords};
        }

        MettingInfo NormalMettingSummarizer::Info() {
            return mMettingInfo;
        }

    }  // namespace lms

}  // namespace qifeng
