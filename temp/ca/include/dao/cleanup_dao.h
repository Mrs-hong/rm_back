//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_DAO_CLEANUP_DAO_H
#define QIFENG_CA_INCLUDE_DAO_CLEANUP_DAO_H

#include <cstdint>

#include "dao/bms_base_dao.h"

namespace qifeng_ca {

    // 数据库清理结果
    struct CleanupDbResult {
        int64_t mDeletedAuditCount = 0;
        int64_t mDeletedDeviceCount = 0;
        bool mSuccess = false;
    };

    // 数据清理DAO: 在一个事务中清理多张表的过期数据
    class CleanupDao : public BmsBaseDao {
    public:
        CleanupDao() = default;
        ~CleanupDao() override = default;

        CleanupDao(const CleanupDao &) = delete;
        CleanupDao &operator=(const CleanupDao &) = delete;
        CleanupDao(CleanupDao &&) = delete;
        CleanupDao &operator=(CleanupDao &&) = delete;

        // 在一个事务中删除 audit_log 和 device 表的过期数据
        // @param auditBeforeTime   审计日志截止时间(毫秒时间戳)
        // @param deviceBeforeTime  设备信息截止时间(毫秒时间戳)
        // @return 清理结果(含删除数量与成功状态)
        CleanupDbResult DeleteExpiredRecords(int64_t auditBeforeTime, int64_t deviceBeforeTime);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_CLEANUP_DAO_H
