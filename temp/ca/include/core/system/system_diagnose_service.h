//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_SYSTEM_SYSTEM_DIAGNOSE_SERVICE_H
#define QIFENG_CA_INCLUDE_CORE_SYSTEM_SYSTEM_DIAGNOSE_SERVICE_H

#include <string>

#include "qifeng_ca/system.pb.h"

#include "common/status.h"

namespace qifeng_ca {

    // 系统诊断日志服务:
    // 收集 logs目录最近10个日志 + 系统版本配置 + 最近3天设备历史信息,
    // 打包为zip返回路径, 并清理压缩前的临时文件
    class SystemDiagnoseService {
    public:
        SystemDiagnoseService() = default;
        ~SystemDiagnoseService() = default;

        SystemDiagnoseService(const SystemDiagnoseService &) = delete;
        SystemDiagnoseService &operator=(const SystemDiagnoseService &) = delete;
        SystemDiagnoseService(SystemDiagnoseService &&) noexcept = default;
        SystemDiagnoseService &operator=(SystemDiagnoseService &&) noexcept = default;

        // 生成诊断日志zip包, 输出zip文件路径到resp->file_url()
        Status GenerateDiagnoseLog(DiagnoseLogResponse* resp);

    private:
        Status CollectLogs(const std::string &destDir);
        Status CollectVersionFile(const std::string &destDir);
        Status CollectDeviceHistory(const std::string &destDir);
        Status CollectAuditLogs(const std::string &destDir);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_SYSTEM_SYSTEM_DIAGNOSE_SERVICE_H
