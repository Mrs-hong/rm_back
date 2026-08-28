//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_CONFIG_HOTWORD_CONFIG_H
#define QIFENG_CA_INCLUDE_COMMON_CONFIG_HOTWORD_CONFIG_H

#include "qifeng_framework/common/config_manager.h"

namespace qifeng_ca {

    class HotWordConfig {
    public:
        static HotWordConfig &GetInstance() {
            static HotWordConfig Instance;
            return Instance;
        }

        int GetHotwordMaxLimit() const {
            int maxLimit = CONFIG_MANAGER.GetInt("hotword", "hotword_max_limit", 500);
            return (maxLimit < 0 || maxLimit > 500) ? 500 : maxLimit;
        }

        int GetHotwordMaxWordLen() const {
            int maxLimit = CONFIG_MANAGER.GetInt("hotword", "hotword_max_word_len", 15);
            return (maxLimit < 0 || maxLimit > 100) ? 100 : maxLimit;
        }

        int GetHotwordMaxRemarkLen() const {
            int maxLimit = CONFIG_MANAGER.GetInt("hotword", "hotword_max_remark_len", 100);
            return (maxLimit < 0 || maxLimit > 200) ? 200 : maxLimit;
        }

    private:
        HotWordConfig() = default;
        ~HotWordConfig() = default;
        HotWordConfig(const HotWordConfig &) = delete;
        HotWordConfig &operator=(const HotWordConfig &) = delete;
        HotWordConfig(HotWordConfig &&) = delete;
        HotWordConfig &operator=(HotWordConfig &&) = delete;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_CONFIG_HOTWORD_CONFIG_H
