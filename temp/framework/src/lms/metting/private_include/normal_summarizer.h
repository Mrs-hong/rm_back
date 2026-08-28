/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_LMS_METTING_PRIVATE_INCLUDE_NORMAL_SUMMARIZER_H
#define QIFENG_FRAMEWORK_LMS_METTING_PRIVATE_INCLUDE_NORMAL_SUMMARIZER_H

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

#include "llm_ability.h"
#include "lms/metting.h"
namespace qifeng {

    namespace lms {

        class ModelContext;

        struct SummaryHints;

        class NormalMettingSummarizer : public MettingSummarizer {
        public:
            NormalMettingSummarizer(std::shared_ptr<Model> model, const NormalMettingInfo& mettingInfo,
                                    const MettingHints& hints = {});

            SummaryResult SynchronouslyExecute(std::function<void(ProgressStat)> progressObserver) override;
            bool Running() override;
            void Stop() override;
            SummaryResult Restart(std::function<void(ProgressStat)> progressObserver) override;

            MettingInfo Info() override;

        private:
            std::shared_ptr<Model> mModel;
            std::shared_ptr<ModelContext> mModelContext;
            std::atomic<bool> mRunning;
            std::mutex mExecutionMutex;
            std::condition_variable mExecutionCV;
            NormalMettingInfo mMettingInfo;
            MettingHints mHints;

            SummaryResult Execute(std::function<void(ProgressStat)> progressObserver);

            // 从纪要范文中提取写作风格并组装 SummaryHints
            SummaryHints BuildAbilityHints(const std::function<void(std::chrono::milliseconds)>& reportProgress);

            // 短文本（< 2500）：极简一次性总结
            SummaryResult SummarizeShort(const std::string& text,
                                         const std::function<void(std::chrono::milliseconds)>& reportProgress);

            // 中等文本（2500 ~ 7500）：分类后一次性总结
            SummaryResult SummarizeMedium(const std::string& text,
                                          const std::function<void(std::chrono::milliseconds)>& reportProgress,
                                          const SummaryHints& abilityHints);

            // 长文本（> 7500）：MapReduce 分块总结
            SummaryResult SummarizeLong(const std::string& text,
                                        const std::function<void(std::chrono::milliseconds)>& reportProgress,
                                        const SummaryHints& abilityHints);
        };

    }  // namespace lms
}  // namespace qifeng

#endif
