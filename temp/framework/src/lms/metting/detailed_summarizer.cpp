#include "spdlog/fmt/bundled/core.h"
#include "spdlog/fmt/bundled/format.h"

#include "common/logger.h"

#include "lms/metting/private_include/detailed_summarizer.h"
#include "lms/metting/private_include/llm_ability.h"
#include "lms/metting/private_include/text_utils.h"
#include "utfcpp/utf8.h"

namespace qifeng {

    namespace lms {

        DetailedMettingSummarizer::DetailedMettingSummarizer(std::shared_ptr<Model> model,
                                                             const DetailedMettingInfo& mettingInfo,
                                                             const MettingHints& hints)
            : mModel {model}, mModelContext {model->CreateContext()}, mRunning {false},
              mMettingInfo {mettingInfo}, mHints {hints} {
        }

        SummaryResult
        DetailedMettingSummarizer::SynchronouslyExecute(std::function<void(ProgressStat)> progressObserver) {
            try {
                {
                    bool expected {false};
                    bool exchanged = mRunning.compare_exchange_strong(expected, true);
                    if (!exchanged) {
                        SLOG_ERROR << "The DetailedMettingSummarizer is running.";
                        return SummaryError {SummaryError::StateCode::SUMMARY_ERROR, "The summarizer is running."};
                    }
                }

                auto result = Execute(progressObserver);

                mRunning = false;
                mExecutionCV.notify_all();

                return result;
            } catch (const ModelContext::CanceledException& e) {
                mRunning = false;
                mExecutionCV.notify_all();

                SLOG_ERROR << "Execution is interrupted, reason = " << e.what();
                return SummaryError {SummaryError::StateCode::INTERRUPTED, e.what()};
            } catch (const std::exception& e) {
                mRunning = false;
                mExecutionCV.notify_all();

                SLOG_ERROR << "Exception is thrown, reason = " << e.what();
                return SummaryError {SummaryError::StateCode::SUMMARY_ERROR, e.what()};
            } catch (...) {
                mRunning = false;
                mExecutionCV.notify_all();

                SLOG_ERROR << "Unknown exception is thrown.";
                return SummaryError {SummaryError::StateCode::SUMMARY_ERROR, ""};
            }
        }

        bool DetailedMettingSummarizer::Running() {
            return mRunning;
        }

        void DetailedMettingSummarizer::Stop() {
            if (mRunning) {
                SLOG_INFO << "DetailedMettingSummarizer stopped by user";
                mModelContext->CancelGeneration();
            }
        }

        SummaryResult
        DetailedMettingSummarizer::Restart(std::function<void(ProgressStat)> progressObserver) {
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

        SummaryHints DetailedMettingSummarizer::BuildAbilityHints(
            const std::function<void(std::chrono::milliseconds)>& reportProgress) {
            SummaryHints abilityHints;
            abilityHints.topic = mHints.topic;

            if (mHints.exampleSummary.empty()) {
                return abilityHints;
            }

            abilityHints.exampleStyle = ExtractStyle(mModelContext, mHints.exampleSummary);
            return abilityHints;
        }

        SummaryResult DetailedMettingSummarizer::Execute(std::function<void(ProgressStat)> progressObserver) {
            uint32_t step = 0;
            std::chrono::system_clock::time_point begin = std::chrono::system_clock::now();
            auto reportProgress = [&progressObserver, &step, &begin](std::chrono::milliseconds estimateRemainingTime) {
                std::chrono::milliseconds spentTime =
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - begin);

                SLOG_DEBUG << "DetailedSummarizer execute " << step << " step, spentTime = " << spentTime.count()
                           << " ms, estimateRemainingTime = " << estimateRemainingTime.count() << " ms";
                step++;

                if (progressObserver) {
                    progressObserver(ProgressStat {spentTime, estimateRemainingTime});
                }
            };

            const auto& mettingInfo = mMettingInfo;
            if (auto err = mettingInfo.Validate(); err) {
                SLOG_ERROR << "DetailedMettingInfo is invalid, code = " << static_cast<int>(err->code)
                           << ", reason = " << err->reason;
                return err.value();
            }

            std::string baseMettingInfo = mettingInfo.FormattedBaseInfo();
            std::string text = fmt::format("{}\n{}", baseMettingInfo, mettingInfo.text);

            SLOG_DEBUG << "DetailedMettingSummarizer start, text bytes: " << text.size()
                      << ", chars: " << Utf8Length(text);

            auto chunkSize = ComputeChunkSize(text);
            std::vector<std::string> documents = SplitText(text, chunkSize);
            if (documents.empty()) {
                throw std::runtime_error {"Documents can not be empty."};
            }

            std::vector<std::string> cleanTexts {};
            for (const auto& document : documents) {
                std::string cleanText = RemoveSerialNumbers(document);
                cleanTexts.push_back(cleanText);
            }
            SLOG_DEBUG << "DetailedSummarizer cleanTexts count: " << cleanTexts.size() << ", chunkSize: " << chunkSize;

            auto abilityHints = BuildAbilityHints(reportProgress);
            // topic / style 默认值（对齐 Python refine_summary）
            if (abilityHints.topic.empty()) {
                abilityHints.topic = "请根据原文内容自行判断会议议题";
            }
            if (abilityHints.exampleStyle.empty()) {
                abilityHints.exampleStyle = "（无参考风格，按标准公文风格撰写）";
            }

            auto rawTextSize = Utf8Length(mettingInfo.text);
            reportProgress(EstimateSummarizeTime(rawTextSize, SummarizeMode::Detailed, chunkSize, cleanTexts.size()));

            std::string summary {};

            if (cleanTexts.size() == 1) {
                SLOG_INFO << "DetailedSummarizer 1-document branch (single chunk)";
                // 当分段数为1时（实际上很少发生），之前的逻辑在这里是把会议内容同时作为开头和结尾，是不对的。这里做了容错处理。

                std::string content =
                    fmt::format("根据以下会议内容，为我生成一篇完整的会议纪要，会议原文：{}", cleanTexts[0]);

                // 议题优先级：用户传入 > LLM 提取
                std::string topic = abilityHints.topic;
                if (topic.empty()) {
                    topic = ExtractTopic(mModelContext, cleanTexts[0]);
                }

                SummaryHints finalHints {topic, abilityHints.exampleStyle};
                summary = OnceSummaryGeneral(mModelContext, content, finalHints);
            } else {
                SLOG_INFO << "DetailedSummarizer multi-document branch, documents: " << cleanTexts.size();
                std::string temporarySummary {};
                {
                    std::string content =
                        fmt::format("根据以下会议开头片段，按照要求为我生成纪要的开头部分：{}", cleanTexts[0]);

                    temporarySummary = RefineFirst(mModelContext, content);
                }

                // RefineFirst 后重估：剩余 middle 步 + last + keywords
                {
                    auto midOut = static_cast<std::size_t>(400 + chunkSize * 0.4);
                    auto remaining = std::chrono::milliseconds {0};
                    if (cleanTexts.size() > 2) {
                        remaining += EstimateCallCost(chunkSize, midOut) * (cleanTexts.size() - 2);
                    }
                    remaining += EstimateCallCost(chunkSize, midOut);
                    remaining += EstimateCallCost(midOut, 30);
                    reportProgress(remaining);
                }

                if (cleanTexts.size() > 2) {
                    for (std::size_t i = 1; i < cleanTexts.size() - 1; i++) {
                        std::string content = fmt::format(
                            "根据以下会议片段以及之前生成的纪要，为我完善纪要，会议片段：{}", cleanTexts[i]);

                        std::string currentSummary = RefineMiddle(mModelContext, content, temporarySummary);

                        auto currentSummaryLength = Utf8Length(currentSummary);
                        auto temporarySummaryLength = Utf8Length(temporarySummary);
                        if (currentSummaryLength < temporarySummaryLength / 2) {
                            SLOG_WARN << "DetailedSummarizer retry chunk " << i << " because output shortened ("
                                      << currentSummaryLength << " < " << (temporarySummaryLength / 2) << " chars)";
                            // 若模型丢失了前文纪要（输出缩短过多），重试一次
                            currentSummary = RefineMiddle(mModelContext, content, temporarySummary);
                        }

                        temporarySummary = currentSummary;
                    }
                }

                // middle 完成后重估：last + keywords
                {
                    auto midOut = static_cast<std::size_t>(400 + chunkSize * 0.4);
                    auto remaining = EstimateCallCost(chunkSize, midOut) + EstimateCallCost(midOut, 30);
                    reportProgress(remaining);
                }

                std::string content = fmt::format(
                    "根据最后一个会议片段以及之前生成的纪要，为我生成一篇完整的会议纪要，最后一个片段原文：{}",
                    cleanTexts.back());

                summary = RefineLast(mModelContext, content, temporarySummary);
            }

            std::vector<std::string> keywords = ExtractKeywords(mModelContext, summary);
            auto spliceKeywords = [](std::string& ov, const std::vector<std::string>& kw) {
                auto pos = ov.find("---");
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
                kwSection += "\n\n---";
                ov.insert(pos + 3, kwSection);
            };

            spliceKeywords(summary, keywords);

            reportProgress(std::chrono::milliseconds {0});
            return SummaryData {summary, keywords};
        }

        MettingInfo DetailedMettingSummarizer::Info() {
            return mMettingInfo;
        }

    }  // namespace lms

}  // namespace qifeng
