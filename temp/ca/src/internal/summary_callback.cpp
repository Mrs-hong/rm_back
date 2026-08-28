//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <chrono>

#include "qifeng_framework/common/logger.h"

#include "common/ws/realtime_dispatch_manager.h"
#include "dao/models/bms_summary.h"
#include "dao/summary_dao.h"
#include "dao_managers/meeting_dao_manager.h"
#include "internal/summary_callback.h"

namespace qifeng_ca {

    // 保存纪要内容到Summary表
    static bool SaveToDb(const std::string &audioId, uint64_t accountId, const std::string &content) {
        models::Summary summary;
        summary.mAccountId = accountId;
        summary.mAudioId = audioId;
        summary.mContent = content;
        summary.mIsOrig = 1;
        summary.mIsDiscard = 0;
        summary.mTimestamp = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());

        SummaryDao dao;
        return dao.Insert(summary);
    }

    // 构造WebSocket推送消息(JSON格式)
    [[maybe_unused]] static std::string BuildSummaryMessage(const std::string &audioId, const SummaryResult &result) {
        std::string json = R"({"audio_id":")" + audioId + R"(","type":"summary","summary":")";

        std::string escaped;
        escaped.reserve(result.mOverviewText.size());
        for (char c : result.mOverviewText) {
            switch (c) {
                case '"':
                    escaped += "\\\"";
                    break;
                case '\\':
                    escaped += "\\\\";
                    break;
                case '\n':
                    escaped += "\\n";
                    break;
                case '\r':
                    escaped += "\\r";
                    break;
                case '\t':
                    escaped += "\\t";
                    break;
                default:
                    escaped += c;
                    break;
            }
        }

        json += escaped + R"("})";
        return json;
    }

    // 通过WebSocket推送纪要结果
    [[maybe_unused]] static void PushToClients(const std::string &audioId, const std::string &message) {
        auto &dispatchMgr = RealtimeDispatchManager::GetInstance();
        bool ok = dispatchMgr.Distribute(message, EClientType::kMessage);
        if (!ok) {
            SLOG_DEBUG << "SummaryCallback: no ws clients for audioId=" << audioId;
        }
    }

    void SummaryCallback::OnSummaryResult(const std::string &audioId, uint64_t accountId, const SummaryResult &result) {
        if (!result.mStatus.IsSuccess()) {
            SLOG_WARN << "SummaryCallback: result error, audioId=" << audioId
                      << " status=" << result.mStatus.ToString();
            return;
        }

        if (result.mOverviewText.empty()) {
            SLOG_WARN << "SummaryCallback: empty summary, audioId=" << audioId;
            return;
        }

        bool saved = SaveToDb(audioId, accountId, result.mOverviewText);
        if (!saved) {
            SLOG_ERROR << "SummaryCallback: save summary failed, audioId=" << audioId;
        }

        if (!result.mKeywords.empty()) {
            auto &daoMgr = MeetingDaoManager::GetInstance();
            bool kwSaved = daoMgr.UpdateKeywords(accountId, audioId, result.mKeywords);
            if (!kwSaved) {
                SLOG_WARN << "SummaryCallback: save keywords failed, audioId=" << audioId;
            }
        }
    }

}  // namespace qifeng_ca
