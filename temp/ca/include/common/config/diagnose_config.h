//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_CONFIG_DIAGNOSE_CONFIG_H
#define QIFENG_CA_INCLUDE_COMMON_CONFIG_DIAGNOSE_CONFIG_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "qifeng_framework/common/config_manager.h"

namespace qifeng_ca {

    // 诊断数据导出配置项
    class DiagnoseConfig {
    public:
        static DiagnoseConfig &GetInstance() {
            static DiagnoseConfig Instance;
            return Instance;
        }

        // 最多拷贝的日志文件数量, 默认10
        size_t GetMaxLogFiles() const {
            int n = CONFIG_MANAGER.GetInt("diagnose", "max_log_files", 10);
            return (n < 1) ? 10 : static_cast<size_t>(n);
        }

        // 诊断数据保留天数(查询设备历史), 默认30
        int64_t GetHistoryDays() const {
            int days = CONFIG_MANAGER.GetInt("diagnose", "history_days", 30);
            return (days < 1) ? 30 : static_cast<int64_t>(days);
        }

        // 审计日志保留天数(查询近30天), 默认30
        int64_t GetAuditLogRetentionDays() const {
            int days = CONFIG_MANAGER.GetInt("diagnose", "audit_log_retention_days", 30);
            return (days < 1) ? 30 : static_cast<int64_t>(days);
        }

        // 审计日志单页最大条数, 默认100
        int32_t GetAuditLogPageSize() const {
            int n = CONFIG_MANAGER.GetInt("diagnose", "audit_log_page_size", 100);
            return (n < 1) ? 100 : static_cast<int32_t>(n);
        }

        // 审计日志最大采集条数(防止内存溢出), 默认10000
        int32_t GetAuditLogMaxRecords() const {
            int n = CONFIG_MANAGER.GetInt("diagnose", "audit_log_max_records", 10000);
            return (n < 1) ? 10000 : static_cast<int32_t>(n);
        }

        // 日志源目录, 默认 logs
        std::string GetLogSrcDir() const { return CONFIG_MANAGER.GetString("diagnose", "log_src_dir", "logs"); }

    private:
        DiagnoseConfig() = default;
        ~DiagnoseConfig() = default;
        DiagnoseConfig(const DiagnoseConfig &) = delete;
        DiagnoseConfig &operator=(const DiagnoseConfig &) = delete;
        DiagnoseConfig(DiagnoseConfig &&) = delete;
        DiagnoseConfig &operator=(DiagnoseConfig &&) = delete;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_CONFIG_DIAGNOSE_CONFIG_H
