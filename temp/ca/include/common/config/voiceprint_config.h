//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_CONFIG_VOICEPRINT_CONFIG_H
#define QIFENG_CA_INCLUDE_COMMON_CONFIG_VOICEPRINT_CONFIG_H

#include "qifeng_framework/common/config_manager.h"

namespace qifeng_ca {

    class VoiceprintConfig {
    public:
        static VoiceprintConfig &GetInstance() {
            static VoiceprintConfig Instance;
            return Instance;
        }

        int GetMaxNumberLen() const {
            int maxLen = CONFIG_MANAGER.GetInt("voiceprint", "max_number_len", 32);
            return (maxLen < 0 || maxLen > 128) ? 128 : maxLen;
        }

        int GetMaxSpeakerLen() const {
            int maxLen = CONFIG_MANAGER.GetInt("voiceprint", "max_speaker_len", 64);
            return (maxLen < 0 || maxLen > 256) ? 256 : maxLen;
        }

        int GetMaxRemarkLen() const {
            int maxLen = CONFIG_MANAGER.GetInt("voiceprint", "max_remark_len", 256);
            return (maxLen < 0 || maxLen > 1024) ? 1024 : maxLen;
        }

        std::string GetVoiceprintPath() const {
            return CONFIG_MANAGER.GetString("voiceprint", "path", "data/audio/voiceprint/");
        }

        int GetLimitRecordDurationSec() const {
            int limit = CONFIG_MANAGER.GetInt("voiceprint", "limit_record_duration_sec", 15);
            return (limit < 0 || limit > 120) ? 15 : limit;
        }

        std::string GetVoiceprintTmpPath() const {
            return CONFIG_MANAGER.GetString("tmp_path", "voiceprint_tmp", "data/voiceprint/");
        }

    private:
        VoiceprintConfig() = default;
        ~VoiceprintConfig() = default;
        VoiceprintConfig(const VoiceprintConfig &) = delete;
        VoiceprintConfig &operator=(const VoiceprintConfig &) = delete;
        VoiceprintConfig(VoiceprintConfig &&) = delete;
        VoiceprintConfig &operator=(VoiceprintConfig &&) = delete;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_CONFIG_VOICEPRINT_CONFIG_H
