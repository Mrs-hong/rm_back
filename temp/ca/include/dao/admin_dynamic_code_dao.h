//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_DAO_ADMIN_DYNAMIC_CODE_DAO_H
#define QIFENG_CA_INCLUDE_DAO_ADMIN_DYNAMIC_CODE_DAO_H

#include <cstdint>
#include <string>

#include "dao/bms_base_dao.h"
#include "dao/models/bms_admin_dynamic_code.h"

namespace qifeng_ca {

    class AdminDynamicCodeDao : public BmsBaseDao {
    public:
        AdminDynamicCodeDao() = default;
        ~AdminDynamicCodeDao() override = default;

        AdminDynamicCodeDao(const AdminDynamicCodeDao &) = delete;
        AdminDynamicCodeDao &operator=(const AdminDynamicCodeDao &) = delete;
        AdminDynamicCodeDao(AdminDynamicCodeDao &&) = delete;
        AdminDynamicCodeDao &operator=(AdminDynamicCodeDao &&) = delete;

        bool Insert(const models::AdminDynamicCode &record);

        models::AdminDynamicCode GetByEncryptionCode(const std::string &encryptionCode);

        bool MarkAsUsedByEncryptionCode(const std::string &encryptionCode);

        bool DeleteByEncryptionCode(const std::string &encryptionCode);

        int32_t CleanExpired(int64_t beforeTimeMs);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_DAO_ADMIN_DYNAMIC_CODE_DAO_H
