//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_EXTERNAL_SERVICES_SERVICE_CONFIG_H
#define QIFENG_CA_EXTERNAL_SERVICES_SERVICE_CONFIG_H

#include <string>

#include "qifeng_framework/common/config_manager.h"

namespace qifeng_ca::external_services {

    enum class CallMode {
        Current = 0,   // 默认当前处理方式
        ExternalHttp,  // 外部 HTTP 云服务调用方式
        Reserved       // 预留
    };

    // 外部配置: 通过不同的服务名获取不同的服务配置
    class ExternalServiceConfig {
    public:
        static ExternalServiceConfig &GetInstance() {
            static ExternalServiceConfig Instance;
            return Instance;
        }

        // 转写调用方式
        CallMode GetTranscribeMode() const {
            int mode = CONFIG_MANAGER.GetInt("external_service", "transcribe_mode", 0);
            return ToCallMode(mode);
        }

        // 总结调用方式
        CallMode GetSummaryMode() const {
            int mode = CONFIG_MANAGER.GetInt("external_service", "summary_mode", 0);
            return ToCallMode(mode);
        }

        // 转写服务名
        std::string GetTranscribeProvider() const {
            return CONFIG_MANAGER.GetString("external_service", "transcribe_provider", "tencent");
        }

        // 总结服务名
        std::string GetSummaryProvider() const {
            return CONFIG_MANAGER.GetString("external_service", "summary_provider", "tencent");
        }

        // 通用: 按 provider+key 读取配置(用于 endpoint/secret/appid 等)
        std::string Get(const std::string &provider, const std::string &key, const std::string &defv = "") const {
            return CONFIG_MANAGER.GetString("external_service." + provider, key, defv);
        }

        int GetInt(const std::string &provider, const std::string &key, int defv) const {
            return CONFIG_MANAGER.GetInt("external_service." + provider, key, defv);
        }

        // HTTP 超时(秒)
        int GetHttpTimeoutSec() const { return CONFIG_MANAGER.GetInt("external_service", "http_timeout_sec", 60); }

    private:
        static CallMode ToCallMode(int v) {
            switch (v) {
                case 1:
                    return CallMode::ExternalHttp;
                case 2:
                    return CallMode::Reserved;
                default:
                    return CallMode::Current;
            }
        }
    };

}  // namespace qifeng_ca::external_services

#endif  // QIFENG_CA_EXTERNAL_SERVICES_SERVICE_CONFIG_H
