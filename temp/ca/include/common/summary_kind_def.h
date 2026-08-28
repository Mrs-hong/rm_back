//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_COMMON_SUMMARY_KIND_DEF_H
#define QIFENG_CA_INCLUDE_COMMON_SUMMARY_KIND_DEF_H

#include <array>
#include <cstdlib>
#include <string_view>

namespace qifeng_ca {

    struct SummaryKindDef {
        std::string_view mLabel;
        int32_t mValue;
        std::string_view mCode;
    };

    constexpr std::array<SummaryKindDef, 4> SummaryKindMap = {{
        {"标准会议", 1, "default"},
        {"党务会议", 2, "cpc"},
        {"决策会议", 3, "decision"},
        {"晨会", 4, "morning"},
    }};

    inline bool IsValidSummaryKind(int32_t kind) {
        for (const auto &item : SummaryKindMap) {
            if (item.mValue == kind) {
                return true;
            }
        }
        return false;
    }

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_COMMON_SUMMARY_KIND_DEF_H
