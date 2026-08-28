//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_DAO_MODELS_BMS_DEVICE_H
#define QIFENG_CA_INCLUDE_DAO_MODELS_BMS_DEVICE_H

#include <cstdint>
#include <string>
#include <vector>

namespace qifeng_ca {
    namespace models {

        struct DeviceRecord {
            std::string mDeviceId;
            int64_t mTimestamp = 0;
            std::string mConnect;
            std::string mVersion;
            std::string mCpu;
            std::string mMemory;
            std::string mDisk;
            std::string mAccelerator;
            std::string mBattery;
            std::string mAudio;
        };

    }  // namespace models
}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_MODELS_BMS_DEVICE_H
