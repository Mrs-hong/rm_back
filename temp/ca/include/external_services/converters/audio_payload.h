//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_EXTERNAL_SERVICES_CONVERTERS_AUDIO_PAYLOAD_H
#define QIFENG_CA_EXTERNAL_SERVICES_CONVERTERS_AUDIO_PAYLOAD_H

#include <string>

#include "drogon/utils/Utilities.h"

#include "external_services/service_types.h"

namespace qifeng_ca::external_services {

    // 输入的参数转换
    inline std::string Build(const AudioInput &in) {
        switch (in.mMode) {
            case AudioInputMode::Base64:
                return in.mData;
            case AudioInputMode::Url:
                return in.mData;
            case AudioInputMode::FileStream: {
                if (in.mBytes.empty()) {
                    return {};
                }
                const auto* p = reinterpret_cast<const unsigned char*>(in.mBytes.data());
                return drogon::utils::base64Encode(p, in.mBytes.size(), true);
            }
            default:
                return {};
        }
    }

}  // namespace qifeng_ca::external_services

#endif  // QIFENG_CA_EXTERNAL_SERVICES_CONVERTERS_AUDIO_PAYLOAD_H
