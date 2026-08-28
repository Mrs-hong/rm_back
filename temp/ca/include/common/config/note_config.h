//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_CONFIG_NOTE_CONFIG_H
#define QIFENG_CA_INCLUDE_COMMON_CONFIG_NOTE_CONFIG_H

#include "qifeng_framework/common/config_manager.h"

namespace qifeng_ca {

    class NoteConfig {
    public:
        static NoteConfig &GetInstance() {
            static NoteConfig Instance;
            return Instance;
        }

        int GetMaxNoteContentLength() const {
            int maxLen = CONFIG_MANAGER.GetInt("note", "max_content_length", 60000);
            return (maxLen < 0 || maxLen > 100000) ? 100000 : maxLen;
        }

    private:
        NoteConfig() = default;
        ~NoteConfig() = default;
        NoteConfig(const NoteConfig &) = delete;
        NoteConfig &operator=(const NoteConfig &) = delete;
        NoteConfig(NoteConfig &&) = delete;
        NoteConfig &operator=(NoteConfig &&) = delete;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_CONFIG_NOTE_CONFIG_H
