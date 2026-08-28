/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_SRC_LMS_PROMPT_PROMPT_DATA_H
#define QIFENG_FRAMEWORK_SRC_LMS_PROMPT_PROMPT_DATA_H

#include <string_view>

namespace qifeng {

    namespace lms {

        namespace detail {

            // ---- 通用工具（来自 General_prompt.py）----
#include "lms/metting/prompts/general_prompt.inc"

            // ---- MapReduce（来自 MapReduce_Prompt.py）----
#include "lms/metting/prompts/map_reduce_prompt.inc"

            // ---- 一次性总结（来自 Once_Prompt.py）----
#include "lms/metting/prompts/once_prompt.inc"

            // ---- 输出模板（来自 OutTemplate_Prompt.py）----
#include "lms/metting/prompts/out_template_prompt.inc"

            // ---- Refine 增量式总结（来自 Refine_Prompt.py）----
#include "lms/metting/prompts/refine_prompt.inc"

            // ---- Simple 模式（来自 Simple_Prompt.py）----
#include "lms/metting/prompts/simple_prompt.inc"

#include "lms/metting/prompts/get_metting_info_prompt.inc"
#include "lms/metting/prompts/metting_info_to_out_prompt.inc"

#include "lms/metting/prompts/once_summary_medium_prompt.inc"
#include "lms/metting/prompts/once_summary_short_prompt.inc"

        }  // namespace detail

    }  // namespace lms

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_SRC_LMS_PROMPT_PROMPT_DATA_H
