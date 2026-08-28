//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <array>
#include <string>
#include <string_view>

#include "qifeng_framework/common/logger.h"
#include "soci/soci.h"

#include "common/status.h"
#include "core/system/reset_task.h"
#include "dao/bms_base_dao.h"

namespace qifeng_ca {

    namespace {

        // 需要清空数据的业务表
        // 不含 casbin_rule / user_group / role 表(不涉及修改)
        // 不含 users 表(由DeleteUsersResetTask处理)
        // 不含 config / system_config 表(配置需保留)
        constexpr std::array<std::string_view, 10> BusinessTables = {
            "audios",  "audit_log", "device", "hot_words", "note",
            "speaker", "summary",   "trans",  "bms_tasks", "admin_dynamic_code",
        };

        // 清空所有业务表数据
        void TruncateBusinessTables(soci::session &session) {
            for (const auto &table : BusinessTables) {
                std::string sql;
                if (table == "device") {
                    // 保留最近一条数据
                    sql = R"(
                        DELETE d1 FROM device d1
                        LEFT JOIN (
                            SELECT device_id, MAX(timestamp) AS max_ts
                            FROM device
                            GROUP BY device_id
                        ) d2 ON d1.device_id = d2.device_id AND d1.timestamp = d2.max_ts
                        WHERE d2.device_id IS NULL
                    )";
                } else {
                    sql = "DELETE FROM ";
                    sql.append(table);
                }
                session << sql;
            }
        }

    }  // namespace

    // 重置业务数据表: 清空会议/日志/热词等业务数据(不修改users/casbin/user_group/role)
    class ResetTablesResetTask final : public ResetTask, public BmsBaseDao {
    public:
        std::string_view Name() const override { return "ResetTables"; }

        Status Execute(uint64_t operatorAccountId) override {
            (void)operatorAccountId;
            try {
                soci::session session = GetSession();
                TruncateBusinessTables(session);
                SLOG_INFO << "Reset: business tables cleared";
                return Status {};
            } catch (const std::exception &e) {
                SLOG_ERROR << "Reset: table reset failed, " << e.what();
                return Status {-1, "数据表重置失败"};
            }
        }
    };

    std::unique_ptr<ResetTask> CreateResetTablesResetTask() {
        return std::make_unique<ResetTablesResetTask>();
    }

}  // namespace qifeng_ca
