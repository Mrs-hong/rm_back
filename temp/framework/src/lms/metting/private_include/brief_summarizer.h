/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_LMS_METTING_PRIVATE_INCLUDE_BRIEF_SUMMARIZER_H
#define QIFENG_FRAMEWORK_LMS_METTING_PRIVATE_INCLUDE_BRIEF_SUMMARIZER_H

#include "lms/metting.h"

namespace qifeng {

    namespace lms {

        class BriefMettingSummarizer;

        class BriefMettingSummarizer : public MettingSummarizer {
        public:
            BriefMettingSummarizer(const BriefMettingInfo& mettingInfo);

            SummaryResult SynchronouslyExecute(std::function<void(ProgressStat)> progressObserver) override;

            bool Running() override;

            void Stop() override;

            SummaryResult Restart(std::function<void(ProgressStat)> progressObserver) override;

            MettingInfo Info() override;

        private:
            BriefMettingInfo mMettingInfo;
        };

    }  // namespace lms
}  // namespace qifeng

#endif