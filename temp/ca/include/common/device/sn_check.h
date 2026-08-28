/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#pragma once

#include <string>
#include <vector>

#include "common/status.h"

namespace qifeng_ca {

    /**
     * @brief 检查SN是否合法 适用于bm1684x核心板
     */
    class SnCheck {
    public:
        struct SnInfo {
            std::string sophon_sn;  // bm1684x核心板SN
            std::string device_sn;  // 设备SN-生产
        };

    public:
        static SnCheck &GetInstance() {
            static SnCheck Instance;
            return Instance;
        }

        // 返回 Status: 成功表示 SN 合法, 失败表示 SN 不合法(含具体原因)
        Status IsSnValid(const std::string &sn);

        bool IsInWhiteList(const std::string &sn);
        // 获得当前设备的SN信息
        // 返回 Status: 成功表示获得SN信息成功, 失败表示获得失败(含具体原因)
        Status GetSelfSnInfo(SnInfo &sn_info);

    private:
        void InitWhiteList();
        SnCheck();

    private:
        std::vector<std::string> mWhiteList;
    };
}  // namespace qifeng_ca
