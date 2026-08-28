#include <cassert>

#include "utfcpp/utf8.h"

#include "spdlog/fmt/bundled/core.h"
#include "spdlog/fmt/bundled/format.h"

#include "lms/metting.h"
#include "lms/metting/private_include/text_utils.h"

#include "lms/metting/private_include/brief_summarizer.h"
#include "lms/metting/private_include/detailed_summarizer.h"
#include "lms/metting/private_include/normal_summarizer.h"

namespace qifeng {

    namespace lms {

        BaseMettingInfo::BaseMettingInfo(const std::string& dateInput, const std::string& locationInput,
                                         const std::string& hostInput, const std::string& attendeesInput,
                                         const std::string& textInput)
            : date {dateInput}, location {locationInput}, host {hostInput}, attendees {attendeesInput},
              text {RemoveSerialNumbers(textInput)} {
        }

        BriefMettingInfo::BriefMettingInfo(const std::string& dateInput, const std::string& locationInput,
                                           const std::string& hostInput, const std::string& attendeesInput,
                                           const std::string& textInput)
            : BaseMettingInfo {dateInput, locationInput, hostInput, attendeesInput, textInput} {
        }

        NormalMettingInfo::NormalMettingInfo(const std::string& dateInput, const std::string& locationInput,
                                             const std::string& hostInput, const std::string& attendeesInput,
                                             const std::string& textInput)
            : BaseMettingInfo {dateInput, locationInput, hostInput, attendeesInput, textInput} {
        }

        DetailedMettingInfo::DetailedMettingInfo(const std::string& dateInput, const std::string& locationInput,
                                                 const std::string& hostInput, const std::string& attendeesInput,
                                                 const std::string& textInput)
            : BaseMettingInfo {dateInput, locationInput, hostInput, attendeesInput, textInput} {
        }

        std::string BaseMettingInfo::FormattedBaseInfo() const {
            constexpr std::string_view kPlaceholder = "[请填写]";
            return fmt::format("【会议基本信息】\n"
                               "会议时间：{}\n"
                               "会议地点：{}\n"
                               "主持人：{}\n"
                               "参会人员：{}\n"
                               "---",
                               date.empty() ? kPlaceholder : std::string_view {date},
                               location.empty() ? kPlaceholder : std::string_view {location},
                               host.empty() ? kPlaceholder : std::string_view {host},
                               attendees.empty() ? kPlaceholder : std::string_view {attendees});
        }

        std::optional<SummaryError> BaseMettingInfo::Validate() const {
            if (!utf8::is_valid(date)) {
                return SummaryError {SummaryError::StateCode::INVALID_DATA, "date is invalid"};
            }
            if (!utf8::is_valid(location)) {
                return SummaryError {SummaryError::StateCode::INVALID_DATA, "location is invalid"};
            }
            if (!utf8::is_valid(host)) {
                return SummaryError {SummaryError::StateCode::INVALID_DATA, "host is invalid"};
            }
            if (!utf8::is_valid(attendees)) {
                return SummaryError {SummaryError::StateCode::INVALID_DATA, "attendees is invalid"};
            }
            if (!utf8::is_valid(text)) {
                return SummaryError {SummaryError::StateCode::INVALID_DATA, "text is invalid"};
            }
            std::size_t count = Utf8Length(text);
            if (count < 500) {
                return SummaryError {SummaryError::StateCode::INPUT_TOO_SHORT, "text is too short"};
            } else if (count > 60000) {
                return SummaryError {SummaryError::StateCode::INPUT_TOO_LONG, "text is too long"};
            }
            return std::nullopt;
        }

        std::optional<SummaryError> BriefMettingInfo::Validate() const {
            return BaseMettingInfo::Validate();
        }

        std::optional<SummaryError> NormalMettingInfo::Validate() const {
            return BaseMettingInfo::Validate();
        }

        std::optional<SummaryError> DetailedMettingInfo::Validate() const {
            return BaseMettingInfo::Validate();
        }

        SummaryResult MettingSummarizer::SynchronouslyExecute() {
            return SynchronouslyExecute(nullptr);
        }

        std::shared_ptr<MettingSummarizer> CreateMettingSummarizer(std::shared_ptr<Model> model,
                                                                   const MettingInfo& mettingInfo) {
            return CreateMettingSummarizer(model, mettingInfo, MettingHints {});
        }

        std::shared_ptr<MettingSummarizer> CreateMettingSummarizer(std::shared_ptr<Model> model,
                                                                   const MettingInfo& mettingInfo,
                                                                   const MettingHints& hints) {
            if (std::holds_alternative<BriefMettingInfo>(mettingInfo)) {
                const auto& info = std::get<BriefMettingInfo>(mettingInfo);

                return std::make_shared<BriefMettingSummarizer>(info);
            } else if (std::holds_alternative<NormalMettingInfo>(mettingInfo)) {
                const auto& info = std::get<NormalMettingInfo>(mettingInfo);

                return std::make_shared<NormalMettingSummarizer>(model, info, hints);
            } else {
                assert(std::holds_alternative<DetailedMettingInfo>(mettingInfo));
                const auto& info = std::get<DetailedMettingInfo>(mettingInfo);

                return std::make_shared<DetailedMettingSummarizer>(model, info, hints);
            }
        }

    }  // namespace lms

}  // namespace qifeng