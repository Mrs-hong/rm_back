//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_DAO_MODELS_BMS_CONFIG_H
#define QIFENG_CA_INCLUDE_DAO_MODELS_BMS_CONFIG_H

#include <cstdint>
#include <string>

namespace qifeng_ca {
    namespace models {

        struct SystemConfig {
            uint64_t mId = 0;
            std::string mConfigKey;
            std::string mConfigValue;
            std::string mDescription;
            int64_t mUpdateTime = 0;
        };

    }  // namespace models
}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_MODELS_BMS_CONFIG_H
