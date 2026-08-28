//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/lms/metting.h"
#include "utf8/checked.h"

#include "common/audio_enums.h"
#include "common/config/meeting_config.h"
#include "common/status.h"
#include "common/summary_kind_def.h"
#include "core/meeting/meeting_access.h"
#include "core/meeting/refresh_summary_service.h"
#include "dao_managers/meeting_dao_manager.h"
#include "schedule/pcm/pcm_engine.h"
#include "schedule/task/summary_task.h"

namespace qifeng_ca {

    namespace {

        // 校验单条议题字符: 仅允许大小写字母、数字、中文
        // 中文范围: U+4E00-U+9FFF (UTF-8: E4 B8 80 - E9 BF BF)
        bool IsTopicCharsValid(std::string_view sv) {
            for (std::size_t i = 0; i < sv.size();) {
                unsigned char ch = static_cast<unsigned char>(sv[i]);
                if (ch < 0x80) {
                    // ASCII: 字母或数字
                    bool isAlphaNum = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
                    if (!isAlphaNum) {
                        return false;
                    }
                    ++i;
                } else if ((ch & 0xF0) == 0xE0 && i + 2 < sv.size()) {
                    // 3字节UTF-8, 校验是否为中文常用字范围 U+4E00-U+9FFF
                    unsigned char b1 = static_cast<unsigned char>(sv[i + 1]);
                    unsigned char b2 = static_cast<unsigned char>(sv[i + 2]);
                    uint32_t code = ((ch & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
                    if (code < 0x4E00 || code > 0x9FFF) {
                        return false;
                    }
                    i += 3;
                } else {
                    // 其他多字节字符(4字节/非法)不允许
                    return false;
                }
            }
            return true;
        }

        // 统计UTF-8字符串的字符数(使用utf8::distance, 与meeting_service保持一致)
        // 注意: utf8::distance(checked)对非法UTF-8会抛异常, 调用前需确保UTF-8合法
        std::size_t CountUtf8Chars(const std::string &s) {
            try {
                return static_cast<std::size_t>(utf8::distance(s.begin(), s.end()));
            } catch (...) {
                // 非法UTF-8回退为字节数(不应发生, IsTopicCharsValid已先行校验)
                return s.size();
            }
        }

        // 校验议题并拼接为换行分隔的文本
        Status ValidateAndJoinTopics(const RefreshSummaryRequest &req, std::string &joined) {
            if (static_cast<std::size_t>(req.topics_size()) > MeetingConfig::GetInstance().GetMaxTopics()) {
                return Status {-1, "议题数量不能超过10条"};
            }
            for (int i = 0; i < req.topics_size(); ++i) {
                const auto &topic = req.topics(i);
                if (topic.empty()) {
                    continue;
                }
                // 先校验字符合法性(含UTF-8结构校验, 不抛异常), 再统计字符数
                // 顺序不能反: CountUtf8Chars对非法UTF-8会抛异常导致core dump
                if (!IsTopicCharsValid(topic)) {
                    return Status {-1, "议题仅支持大小写字母、数字、中文"};
                }
                if (CountUtf8Chars(topic) > MeetingConfig::GetInstance().GetMaxTopicLen()) {
                    return Status {-1, "单条议题长度不能超过50字符"};
                }
                if (!joined.empty()) {
                    joined += "\n";
                }
                joined += topic;
            }
            return {};
        }

        // 校验音频可刷新: 访问权限 + 状态为SUMMARY_COMPLETE
        Status CheckAudioRefreshable(uint64_t accountId, const std::string &audioId, models::Audio* outAudio) {
            auto [audio, accessStatus] = GetAccessibleAudio(accountId, audioId);
            if (!accessStatus.IsSuccess()) {
                SLOG_WARN << "RefreshSummary: access denied, audioId=" << audioId;
                return accessStatus;
            }
            if (audio.mStatus != static_cast<int>(AudioStatus::SummaryComplete)) {
                SLOG_WARN << "RefreshSummary: audio status not SUMMARY_COMPLETE, status=" << audio.mStatus;
                return Status {-1, "当前状态不允许重新生成纪要"};
            }
            if (outAudio != nullptr) {
                *outAudio = audio;
            }
            return {};
        }

        // 校验刷新参数(会议类型 + 字数)
        Status ValidateRefreshParams(int kind, int wordCount) {
            if (!IsValidSummaryKind(kind)) {
                return Status {-1, "无效的会议类型"};
            }
            if (wordCount < 0 || wordCount > 50000) {
                return Status {-1, "wordCount超出范围"};
            }
            return {};
        }

        // 构建LMS提示信息(议题 + 范文)
        [[maybe_unused]] qifeng::lms::MettingHints BuildHints(const std::string &topic,
                                                              const std::string &templateText) {
            qifeng::lms::MettingHints hints;
            hints.topic = topic;
            hints.exampleSummary = templateText;
            return hints;
        }

        // 提交SummaryTask到PCM引擎
        [[maybe_unused]] Status SubmitSummaryTask(const std::string &audioId, uint64_t ownerId,
                                                  const qifeng::lms::MettingHints &hints) {
            auto summaryTask = std::make_shared<SummaryTask>(audioId, ownerId);
            summaryTask->SetHints(hints);
            PcmEngine::GetInstance().Submit(summaryTask);
            return {};
        }

    }  // namespace

    Status RefreshSummaryService::RefreshSummary(const RefreshSummaryRequest &req, RefreshSummaryResponse* resp) {
        uint64_t accountId = req.account_id();
        const std::string &audioId = req.audio_id();
        int kind = req.kind();
        int wordCount = req.word_count();

        SLOG_DEBUG << "RefreshSummary: accountId=" << accountId << " audioId=" << audioId << " kind=" << kind
                   << " wordCount=" << wordCount << " topicCount=" << req.topics_size()
                   << " templateLen=" << req.template_text().size();

        Status validStatus = ValidateRefreshParams(kind, wordCount);
        if (!validStatus.IsSuccess()) {
            return validStatus;
        }

        std::string joinedTopic;
        Status topicStatus = ValidateAndJoinTopics(req, joinedTopic);
        if (!topicStatus.IsSuccess()) {
            return topicStatus;
        }

        models::Audio audio;
        Status checkStatus = CheckAudioRefreshable(accountId, audioId, &audio);
        if (!checkStatus.IsSuccess()) {
            return checkStatus;
        }

        RefreshSummaryParams params;
        params.mKind = kind;
        params.mWordCount = wordCount;
        params.mUseNote = req.use_note();
        auto &daoMgr = MeetingDaoManager::GetInstance();
        if (!daoMgr.UpdateForRefreshSummary(audio.mAccountId, audioId, params)) {
            SLOG_ERROR << "RefreshSummary: DB update failed, audioId=" << audioId;
            return Status {-1, "更新失败"};
        }

        auto hints = BuildHints(joinedTopic, req.template_text());
        Status submitStatus = SubmitSummaryTask(audioId, audio.mAccountId, hints);
        if (!submitStatus.IsSuccess()) {
            return submitStatus;
        }

        resp->set_message("重新总结成功,会议类型为: " + std::to_string(kind));
        SLOG_INFO << "RefreshSummary: success, audioId=" << audioId << " kind=" << kind;
        return {};
    }

}  // namespace qifeng_ca
