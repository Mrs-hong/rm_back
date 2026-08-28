//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_CONFIG_MEETING_CONFIG_H
#define QIFENG_CA_INCLUDE_COMMON_CONFIG_MEETING_CONFIG_H

#include "qifeng_framework/common/config_manager.h"
#include <cstddef>

namespace qifeng_ca {

    class MeetingConfig {
    public:
        static MeetingConfig &GetInstance() {
            static MeetingConfig Instance;
            return Instance;
        }

        int GetMaxThemeLen() const {
            int maxLen = CONFIG_MANAGER.GetInt("meeting", "max_theme_len", 64);
            return (maxLen < 0 || maxLen > 256) ? 256 : maxLen;
        }

        int GetMaxModeratorLen() const {
            int maxLen = CONFIG_MANAGER.GetInt("meeting", "max_moderator_len", 64);
            return (maxLen < 0 || maxLen > 256) ? 256 : maxLen;
        }

        int GetMaxPlacesLen() const {
            int maxLen = CONFIG_MANAGER.GetInt("meeting", "max_places_len", 64);
            return (maxLen < 0 || maxLen > 256) ? 256 : maxLen;
        }

        int GetMaxRemarkLen() const {
            int maxLen = CONFIG_MANAGER.GetInt("meeting", "max_remark_len", 128);
            return (maxLen < 0 || maxLen > 512) ? 512 : maxLen;
        }

        std::string GetAudioPath() const { return CONFIG_MANAGER.GetString("meeting", "audio_path", "data/audio"); }

        // 返回软连接目录下的音频路径(如 data/src/audio), 通过配置文件动态调整
        std::string GetSymlinkAudioPath() const {
            return CONFIG_MANAGER.GetString("meeting", "symlink_audio_path", "data/src/audio");
        }

        int GetMaxUploadFileSizeMb() const {
            int mb = CONFIG_MANAGER.GetInt("meeting", "max_upload_file_size_mb", 2150);
            return (mb < 1 || mb > 10240) ? 10240 : mb;
        }

        int GetMaxUploadTotalSizeMb() const {
            int mb = CONFIG_MANAGER.GetInt("meeting", "max_upload_total_size_mb", 10240);
            return (mb < 1 || mb > 20480) ? 20480 : mb;
        }

        int GetMinRecordingSeconds() const {
            int sec = CONFIG_MANAGER.GetInt("meeting", "min_recording_seconds", 15);
            return (sec < 1 || sec > 600) ? 15 : sec;
        }

        int GetLimitRecordTimeSec() const {
            int sec = CONFIG_MANAGER.GetInt("meeting", "limit_record_time", 10800);
            return (sec < 60 || sec > 18000) ? 10800 : sec;
        }

        int GetMaxAttendees() const {
            int maxAttendees = CONFIG_MANAGER.GetInt("meeting", "max_attendees", 256);
            return (maxAttendees < 0 || maxAttendees > 512) ? 256 : maxAttendees;
        }

        int GetMaxSpeakerLen() const {
            int maxLen = CONFIG_MANAGER.GetInt("meeting", "max_speaker_len", 64);
            return (maxLen < 0 || maxLen > 256) ? 256 : maxLen;
        }

        int GetMaxContentLen() const {
            int maxLen = CONFIG_MANAGER.GetInt("meeting", "max_content_len", 512);
            return (maxLen < 0 || maxLen > 1024) ? 1024 : maxLen;
        }

        int GetMaxSummaryLen() const {
            int maxLen = CONFIG_MANAGER.GetInt("meeting", "max_summary_len", 50000);
            return (maxLen < 0 || maxLen > 100000) ? 100000 : maxLen;
        }

        // 录音暂停最大时长(秒), 超时后自动停止会议, 默认20分钟
        int GetMaxPauseDurationSec() const {
            int sec = CONFIG_MANAGER.GetInt("meeting", "max_pause_duration_sec", 1200);
            return (sec < 60 || sec > 86400) ? 86400 : sec;
        }

        size_t GetMaxTopics() const {
            int maxTopics = CONFIG_MANAGER.GetInt("meeting", "max_topics", 10);
            return (maxTopics < 0 || maxTopics > 100) ? 100 : maxTopics;
        }

        size_t GetMaxTopicLen() const {
            int maxLen = CONFIG_MANAGER.GetInt("meeting", "max_topic_len", 64);
            return (maxLen < 0 || maxLen > 256) ? 256 : maxLen;
        }

    private:
        MeetingConfig() = default;
        ~MeetingConfig() = default;
        MeetingConfig(const MeetingConfig &) = delete;
        MeetingConfig &operator=(const MeetingConfig &) = delete;
        MeetingConfig(MeetingConfig &&) = delete;
        MeetingConfig &operator=(MeetingConfig &&) = delete;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_CONFIG_MEETING_CONFIG_H
