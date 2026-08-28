/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_LMS_METTING_PRIVATE_INCLUDE_DETAILED_SUMMARIZER_H
#define QIFENG_FRAMEWORK_LMS_METTING_PRIVATE_INCLUDE_DETAILED_SUMMARIZER_H

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

#include "lms/metting.h"

namespace qifeng {

    namespace lms {

        class ModelContext;

        struct SummaryHints;

        class DetailedMettingSummarizer : public MettingSummarizer {
        public:
            DetailedMettingSummarizer(std::shared_ptr<Model> model, const DetailedMettingInfo& mettingInfo,
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
            DetailedMettingInfo mMettingInfo;
            MettingHints mHints;

            SummaryResult Execute(std::function<void(ProgressStat)> progressObserver);

            // 从纪要范文中提取写作风格并组装 SummaryHints
            SummaryHints BuildAbilityHints(const std::function<void(std::chrono::milliseconds)>& reportProgress);
        };

    }  // namespace lms
}  // namespace qifeng

#endif