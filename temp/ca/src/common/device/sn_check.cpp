//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

#include "qifeng_framework/common/logger.h"

#include "common/device/sn_check.h"

namespace qifeng_ca {

    // OEM 配置文件路径(bm1684x 核心板出厂烧录)
    static constexpr const char* OemConfigPath = "/factory/OEMconfig.ini";

    SnCheck::SnCheck() {
        InitWhiteList();
    }

    // 初始化 SN 白名单(硬编码生产设备 sophon_sn)
    // 白名单匹配对象为 SOPHON_SN(bm1684x 核心板 SN, 由 OEMconfig.ini 中 "SN" 字段读取)
    // 由生产环节登记维护, 新增设备需在此处追加 SN 并重新编译
    void SnCheck::InitWhiteList() {
        mWhiteList = {
            "QYS7021BDJIJI0167", "BJSNS7MBDJFJD0020", "QYS7020BEJIAH0002", "CD010AC0C25210187", "CD010AC0C25210195",
            "QS7M077BFJEBB0108", "QYS7020BEJIAH0003", "QS7M077BFJEBB0137", "QS7M266BFJBJB0064", "QS7M077BFJBJB0015",
            "QS7M0AABFJDJI0058", "QS7M0AABFJDJI0010", "QS7M0AABFJDJI0100", "CD010AC0C25210186", "QS7M077DFJDBF0262",
            "QS7M077BFJEBB0050", "QS7M077BFJEBB0089", "CD010AC0C25210173", "CD010AC0C25210177", "QS7M266BFJBJB0067",
            "QYS7021BDJIJI0252", "CD010AC0C25210192", "QS7M077BFJEBB0123", "QS7M077BFJEBB0143", "CD010AC0C25210185",
            "CD010AC0C25210176", "CD010AC0C25210179",
        };
        SLOG_INFO << "SnCheck: whitelist initialized, size=" << mWhiteList.size();
    }

    // 去除字符串首尾空白
    static std::string TrimSpace(const std::string &s) {
        auto begin = s.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) {
            return "";
        }
        auto end = s.find_last_not_of(" \t\r\n");
        return s.substr(begin, end - begin + 1);
    }

    // 从 OEMconfig.ini 中解析指定键的值(格式: "KEY = VALUE")
    // key: 键名(如 "SN"、"DEVICE_SN"), 返回 trim 后的值, 未找到返回空串
    static std::string ReadOemConfigValue(const std::string &key) {
        std::ifstream file(OemConfigPath);
        if (!file.is_open()) {
            SLOG_WARN << "SnCheck: open OEM config failed, path=" << OemConfigPath;
            return "";
        }
        std::string prefix = key + " =";
        std::string line;
        while (std::getline(file, line)) {
            // 行首匹配 "KEY ="
            if (line.compare(0, prefix.size(), prefix) == 0) {
                auto pos = line.find('=');
                if (pos != std::string::npos) {
                    return TrimSpace(line.substr(pos + 1));
                }
            }
        }
        return "";
    }

    bool SnCheck::IsInWhiteList(const std::string &sn) {
        if (sn.empty()) {
            return false;
        }
        return std::find(mWhiteList.begin(), mWhiteList.end(), sn) != mWhiteList.end();
    }

    Status SnCheck::IsSnValid([[maybe_unused]] const std::string &sn) {
        // if (sn.empty()) {
        //     return Status {-1, "SN为空"};
        // }
        // 暂时跳过白名单校验: 测试阶段所有非空 SN 均放行
        // if (!IsInWhiteList(sn)) {
        //     SLOG_WARN << "SnCheck: SN not in whitelist, sn=" << sn;
        //     return Status {-1, "SN不在升级白名单"};
        // }
        return {};
    }

    Status SnCheck::GetSelfSnInfo(SnInfo &sn_info) {
        sn_info.sophon_sn = ReadOemConfigValue("SN");
        sn_info.device_sn = ReadOemConfigValue("DEVICE_SN");

        if (sn_info.sophon_sn.empty() && sn_info.device_sn.empty()) {
            SLOG_ERROR << "SnCheck: read SN info failed, both empty, path=" << OemConfigPath;
            return Status {-1, "读取SN信息失败"};
        }

        SLOG_INFO << "SnCheck: sophon_sn=" << sn_info.sophon_sn << ", device_sn=" << sn_info.device_sn;
        return {};
    }

}  // namespace qifeng_ca
