#include "lms/metting/private_include/brief_summarizer.h"

namespace qifeng {

    namespace lms {

        BriefMettingSummarizer::BriefMettingSummarizer(const BriefMettingInfo& mettingInfo)
            : mMettingInfo {mettingInfo} {
        }

        SummaryResult BriefMettingSummarizer::SynchronouslyExecute(std::function<void(ProgressStat)> progressObserver) {
            return SummaryError {SummaryError::StateCode::SUMMARY_ERROR, "Not implement yet."};
        }

        bool BriefMettingSummarizer::Running() {
            return false;
        }

        void BriefMettingSummarizer::Stop() {
        }

        SummaryResult BriefMettingSummarizer::Restart(std::function<void(ProgressStat)> progressObserver) {
            return SummaryError {SummaryError::StateCode::SUMMARY_ERROR, "Not implement yet."};
        }

        MettingInfo BriefMettingSummarizer::Info() {
            return mMettingInfo;
        }

    }  // namespace lms

}  // namespace qifeng