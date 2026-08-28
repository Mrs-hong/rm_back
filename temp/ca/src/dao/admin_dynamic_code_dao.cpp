//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <cstdint>
#include <string>

#include "qifeng_framework/common/logger.h"

#include "dao/admin_dynamic_code_dao.h"

namespace qifeng_ca {

    bool AdminDynamicCodeDao::Insert(const models::AdminDynamicCode &record) {
        try {
            soci::session sql = GetSession();
            int32_t isUsed = record.mIsUsed ? 1 : 0;
            soci::statement stmt =
                (sql.prepare << "INSERT INTO admin_dynamic_code (dynamic, encryption_code, expire_time, is_used, "
                                "create_time) VALUES (:dynamic, :ecode, :expire, :used, :ctime)",
                 soci::use(record.mDynamic, "dynamic"), soci::use(record.mEncryptionCode, "ecode"),
                 soci::use(record.mExpireTime, "expire"), soci::use(isUsed, "used"),
                 soci::use(record.mCreateTime, "ctime"));
            stmt.execute(true);
            return stmt.get_affected_rows() > 0;
        } catch (const soci::soci_error &e) {
            SLOG_ERROR << "AdminDynamicCodeDao::Insert failed: " << e.what();
        } catch (const std::exception &e) {
            SLOG_ERROR << "AdminDynamicCodeDao::Insert exception: " << e.what();
        }
        return false;
    }

    models::AdminDynamicCode AdminDynamicCodeDao::GetByEncryptionCode(const std::string &encryptionCode) {
        models::AdminDynamicCode record;
        try {
            soci::session sql = GetSession();
            int32_t isUsed = 0;
            soci::statement stmt =
                (sql.prepare << "SELECT id, dynamic, encryption_code, expire_time, is_used, create_time "
                                "FROM admin_dynamic_code WHERE encryption_code = :ecode",
                 soci::into(record.mId), soci::into(record.mDynamic), soci::into(record.mEncryptionCode),
                 soci::into(record.mExpireTime), soci::into(isUsed), soci::into(record.mCreateTime),
                 soci::use(encryptionCode, "ecode"));
            bool found = stmt.execute(true);
            if (found) {
                record.mIsUsed = (isUsed != 0);
            }
        } catch (const soci::soci_error &e) {
            SLOG_ERROR << "AdminDynamicCodeDao::GetByEncryptionCode failed: " << e.what();
        } catch (const std::exception &e) {
            SLOG_ERROR << "AdminDynamicCodeDao::GetByEncryptionCode exception: " << e.what();
        }
        return record;
    }

    bool AdminDynamicCodeDao::MarkAsUsedByEncryptionCode(const std::string &encryptionCode) {
        try {
            soci::session sql = GetSession();
            soci::statement stmt =
                (sql.prepare << "UPDATE admin_dynamic_code SET is_used = 1 WHERE encryption_code = :ecode",
                 soci::use(encryptionCode, "ecode"));
            stmt.execute(true);
            return stmt.get_affected_rows() > 0;
        } catch (const soci::soci_error &e) {
            SLOG_ERROR << "AdminDynamicCodeDao::MarkAsUsedByEncryptionCode failed: " << e.what();
        } catch (const std::exception &e) {
            SLOG_ERROR << "AdminDynamicCodeDao::MarkAsUsedByEncryptionCode exception: " << e.what();
        }
        return false;
    }

    bool AdminDynamicCodeDao::DeleteByEncryptionCode(const std::string &encryptionCode) {
        try {
            soci::session sql = GetSession();
            soci::statement stmt = (sql.prepare << "DELETE FROM admin_dynamic_code WHERE encryption_code = :ecode",
                                    soci::use(encryptionCode, "ecode"));
            stmt.execute(true);
            return stmt.get_affected_rows() > 0;
        } catch (const soci::soci_error &e) {
            SLOG_ERROR << "AdminDynamicCodeDao::DeleteByEncryptionCode failed: " << e.what();
        } catch (const std::exception &e) {
            SLOG_ERROR << "AdminDynamicCodeDao::DeleteByEncryptionCode exception: " << e.what();
        }
        return false;
    }

    int32_t AdminDynamicCodeDao::CleanExpired(int64_t beforeTimeMs) {
        try {
            soci::session sql = GetSession();
            soci::statement stmt = (sql.prepare << "DELETE FROM admin_dynamic_code WHERE expire_time < :before",
                                    soci::use(beforeTimeMs, "before"));
            stmt.execute(true);
            return static_cast<int32_t>(stmt.get_affected_rows());
        } catch (const soci::soci_error &e) {
            SLOG_ERROR << "AdminDynamicCodeDao::CleanExpired failed: " << e.what();
        } catch (const std::exception &e) {
            SLOG_ERROR << "AdminDynamicCodeDao::CleanExpired exception: " << e.what();
        }
        return 0;
    }

}  // namespace qifeng_ca
