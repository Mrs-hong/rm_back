//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include "core/dashboard/dashboard_service.h"

#include <algorithm>

#include "common/audio_enums.h"
#include "dao/audio_dao.h"
#include "dao_managers/meeting_dao_manager.h"
#include "qifeng_framework/common/logger.h"

namespace qifeng_ca {

    namespace {
        // 显示屏可用时长上限(小时)
        constexpr int32_t DisplayMaxHours = 3000;

        // 已生成: 纪要完成(非失败)
        const std::vector<int> &DoneStatusList() {
            static const std::vector<int> List {static_cast<int>(AudioStatus::SummaryComplete)};
            return List;
        }

        // 待生成: 待转写 + 转写中 + 待总结 + 总结中
        const std::vector<int> &PendingStatusList() {
            static const std::vector<int> List {
                static_cast<int>(AudioStatus::WaitTrans),
                static_cast<int>(AudioStatus::Transing),
                static_cast<int>(AudioStatus::WaitSummary),
                static_cast<int>(AudioStatus::Summarying),
            };
            return List;
        }

        // 采集磁盘可用时长信息(填充response)
        void CollectDiskInfo(DiskAvailableInfoResponse &resp) {
            auto info = DashboardService::CollectDiskTimeInfo();
            resp.set_available_time(info.availableHours);
            resp.set_available_time_prop(info.availableHours);
            resp.set_total_time(info.totalHours);
            resp.set_available_time_ratio(std::to_string(info.ratioPercent) + "%");
        }

        // 采集纪要统计信息(基于audios表)
        void CollectSummary(SummaryStatsResponse &resp) {
            AudioDao audioDao;
            int64_t done = audioDao.CountByStatus(DoneStatusList());
            int64_t pending = audioDao.CountByStatus(PendingStatusList());

            int32_t total = static_cast<int32_t>(done + pending);
            resp.set_done(static_cast<int32_t>(done));
            resp.set_waiting(static_cast<int32_t>(pending));
            resp.set_total(total);
            if (total > 0) {
                resp.set_ratio((static_cast<int32_t>(done) * 100) / total);
            } else {
                resp.set_ratio(0);
            }
        }
    }  // namespace

    // 采集磁盘可用时长信息(页面与显示屏复用, 保证一致)
    // 按3000小时上限, 根据audios表总时长计算剩余可用时长
    DiskTimeInfo DashboardService::CollectDiskTimeInfo() {
        DiskTimeInfo info;
        info.totalHours = DisplayMaxHours;

        int64_t totalMs = MeetingDaoManager::GetInstance().GetTotalDuration();
        int32_t usedHours = static_cast<int32_t>(totalMs / (1000LL * 60 * 60));
        info.availableHours = std::max(static_cast<int32_t>(0), info.totalHours - usedHours);
        info.ratioPercent = (info.totalHours > 0) ? (info.availableHours * 100 / info.totalHours) : 0;
        return info;
    }

    Status DashboardService::GetDiskAvailableInfo(DiskAvailableInfoResponse* resp) {
        CollectDiskInfo(*resp);
        SLOG_DEBUG << "DashboardService: getDiskAvailableInfo, availHours=" << resp->available_time()
                   << " totalHours=" << resp->total_time();
        return Status {};
    }

    Status DashboardService::GetSummaryStats(SummaryStatsResponse* resp) {
        CollectSummary(*resp);
        SLOG_DEBUG << "DashboardService: getSummaryStats, done=" << resp->done() << " waiting=" << resp->waiting()
                   << " total=" << resp->total();
        return Status {};
    }

}  // namespace qifeng_ca
