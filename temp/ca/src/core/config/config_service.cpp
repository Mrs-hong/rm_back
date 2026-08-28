//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <string_view>

#include "qifeng_framework/common/logger.h"

#include "common/common.h"
#include "common/proto_utils.h"
#include "core/config/config_service.h"
#include "dao/models/bms_config.h"

namespace qifeng_ca {

    static constexpr std::string_view SysBaseConfig = "SYS_BASE_CONFIG";
    static constexpr std::string_view BaseConfig = "BASE_CONFIG";

    static bool ValidateLogCleanDays(int32_t days) {
        return days > 0;
    }

    static bool ValidateDiskWarning(int32_t threshold) {
        return threshold > 0 && threshold < 100;
    }

    static bool ValidateDiskLimit(int32_t threshold) {
        return threshold > 0 && threshold < 100;
    }

    static bool ValidateDiskThreshold(int32_t warning, int32_t limit) {
        return warning < limit;
    }

    std::string ConfigService::GetConfigJson(const std::string_view key, const std::string &defaultJson) {
        models::SystemConfig cfg = mConfigDao.GetByKey(key.data());
        if (cfg.mId == 0 || cfg.mConfigValue.empty()) {
            return defaultJson;
        }
        return cfg.mConfigValue;
    }

    bool ConfigService::SetConfigJson(const std::string_view key, const std::string &jsonValue) {
        models::SystemConfig cfg;
        cfg.mConfigKey = key.data();
        cfg.mConfigValue = jsonValue;
        cfg.mUpdateTime = static_cast<int64_t>(GetTimeMs());
        return mConfigDao.Upsert(cfg);
    }

    Status ConfigService::GetWebBaseConfig(GetBaseConfigResponse* resp) {
        std::string jsonStr = GetConfigJson(BaseConfig, "{}");
        if (!jsonStr.empty() && jsonStr != "{}") {
            if (!proto_utils::JsonToMessage(jsonStr, *resp)) {
                SLOG_WARN << "GetWebBaseConfig parse from DB failed, using defaults";
            }
        }

        // 默认90天
        if (resp->log_clean_days() == 0) {
            resp->set_log_clean_days(90);
        }

        SLOG_INFO << "GetWebBaseConfig - logCleanDays: " << resp->log_clean_days();
        return Status {};
    }

    Status ConfigService::SetWebBaseConfig(const SetBaseConfigRequest &req) {
        SLOG_INFO << "SetWebBaseConfig - logCleanDays: " << req.log_clean_days();

        if (!ValidateLogCleanDays(req.log_clean_days())) {
            return Status {-1, "操作记录保留天数必须大于0"};
        }

        GetBaseConfigResponse currentCfg;
        std::string jsonStr = GetConfigJson(BaseConfig, "{}");
        if (!jsonStr.empty() && jsonStr != "{}") {
            proto_utils::JsonToMessage(jsonStr, currentCfg);
        }

        currentCfg.set_log_clean_days(req.log_clean_days());
        std::string newJson = proto_utils::MessageToJson(currentCfg);
        if (newJson.empty()) {
            return Status {-1, "配置项不合理"};
        }

        if (!SetConfigJson("BASE_CONFIG", newJson)) {
            return Status {-1, "保存配置失败"};
        }

        return Status {};
    }

    Status ConfigService::GetSysBaseConfig(GetSysBaseConfigResponse* resp) {
        std::string jsonStr = GetConfigJson(SysBaseConfig, "{}");
        if (!jsonStr.empty() && jsonStr != "{}") {
            if (!proto_utils::JsonToMessage(jsonStr, *resp)) {
                SLOG_WARN << "GetSysBaseConfig parse from DB failed, using defaults";
            }
        }

        // 默认配置
        if (resp->disk_warning() == 0) {
            resp->set_disk_warning(80);
        }
        if (resp->disk_limit() == 0) {
            resp->set_disk_limit(95);
        }

        SLOG_INFO << "GetSysBaseConfig - diskWarning: " << resp->disk_warning()
                  << ", diskLimit: " << resp->disk_limit();
        return Status {};
    }

    Status ConfigService::SetSysBaseConfig(const SetSysBaseConfigRequest &req) {
        SLOG_INFO << "SetSysBaseConfig - diskWarning: " << req.disk_warning() << ", diskLimit: " << req.disk_limit();

        if (!ValidateDiskWarning(req.disk_warning())) {
            return Status {-1, "磁盘预警阈值必须大于0且小于100"};
        }
        if (!ValidateDiskLimit(req.disk_limit())) {
            return Status {-1, "磁盘限制阈值必须大于0且小于100"};
        }
        if (!ValidateDiskThreshold(req.disk_warning(), req.disk_limit())) {
            return Status {-1, "预警阈值必须小于限制阈值"};
        }

        GetSysBaseConfigResponse currentCfg;
        std::string jsonStr = GetConfigJson(SysBaseConfig, "{}");
        if (!jsonStr.empty() && jsonStr != "{}") {
            proto_utils::JsonToMessage(jsonStr, currentCfg);
        }

        currentCfg.set_disk_warning(req.disk_warning());
        currentCfg.set_disk_limit(req.disk_limit());
        std::string newJson = proto_utils::MessageToJson(currentCfg);
        if (newJson.empty()) {
            return Status {-1, "配置项不合理"};
        }

        if (!SetConfigJson("SYS_BASE_CONFIG", newJson)) {
            return Status {-1, "保存配置失败"};
        }

        return Status {};
    }

}  // namespace qifeng_ca
