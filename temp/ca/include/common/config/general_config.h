//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_CONFIG_GENERAL_CONFIG_H
#define QIFENG_CA_INCLUDE_COMMON_CONFIG_GENERAL_CONFIG_H

#include "qifeng_framework/common/config_manager.h"

namespace qifeng_ca {

    class GeneralConfig {
    public:
        static GeneralConfig &GetInstance() {
            static GeneralConfig Instance;
            return Instance;
        }

        int GetLogLevel() const {
            int level = CONFIG_MANAGER.GetInt("general", "log_level", 1);
            return (level < 0 || level > 6) ? 1 : level;
        }

    private:
        GeneralConfig() = default;
        ~GeneralConfig() = default;
        GeneralConfig(const GeneralConfig &) = delete;
        GeneralConfig &operator=(const GeneralConfig &) = delete;
        GeneralConfig(GeneralConfig &&) = delete;
        GeneralConfig &operator=(GeneralConfig &&) = delete;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_CONFIG_GENERAL_CONFIG_H
