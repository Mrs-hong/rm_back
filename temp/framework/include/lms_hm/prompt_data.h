/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_LMS_HM_PROMPT_DATA_H
#define QIFENG_FRAMEWORK_INCLUDE_LMS_HM_PROMPT_DATA_H

#include <string_view>

namespace qifeng {
    namespace lmshm {
        namespace prompt {
#include "lms_hm/prompts/extract_example_style_prompt.inc"

#include "lms_hm/prompts/extract_keyword_prompt.inc"

#include "lms_hm/prompts/extract_topic_prompt.inc"

#include "lms_hm/prompts/extract_meeting_type_prompt.inc"

#include "lms_hm/prompts/overview_once_prompt.inc"

        }  // namespace prompt

    }  // namespace lmshm

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_INCLUDE_LMS_HM_PROMPT_DATA_H
