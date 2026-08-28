//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_MEETING_REFRESH_SUMMARY_SERVICE_H
#define QIFENG_CA_INCLUDE_CORE_MEETING_REFRESH_SUMMARY_SERVICE_H

#include "qifeng_ca/meeting.pb.h"

#include "common/status.h"

namespace qifeng_ca {

    // 重新生成会议纪要服务: 校验参数 → 校验音频可刷新 → 更新DB → 提交SummaryTask
    // 支持外部传入议题(topics)和自定义模板文本(templateText):
    //   - topics 非空时, 拼接为议题文本传入LMS, 跳过LLM自动提取
    //   - templateText 非空时, 作为纪要范文传入LMS, 学习其写作风格
    class RefreshSummaryService {
    public:
        RefreshSummaryService() = default;
        ~RefreshSummaryService() = default;

        RefreshSummaryService(const RefreshSummaryService &) = delete;
        RefreshSummaryService &operator=(const RefreshSummaryService &) = delete;
        RefreshSummaryService(RefreshSummaryService &&) noexcept = delete;
        RefreshSummaryService &operator=(RefreshSummaryService &&) = delete;

        // 重新生成会议纪要(支持议题和自定义模板)
        Status RefreshSummary(const RefreshSummaryRequest &req, RefreshSummaryResponse* resp);
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_MEETING_REFRESH_SUMMARY_SERVICE_H
