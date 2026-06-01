/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/scmd_types.h"

namespace qifeng {
    namespace scm {

        ResultMsg MakeResult(int code, const std::string &msg) {
            return ResultMsg(code, msg);
        }

        ResultMsg MakeSuccess() {
            return ResultMsg(0, "success");
        }

        ResultMsg MakeError(const std::string &msg) {
            return ResultMsg(-1, msg);
        }

        ResultMsg MakeWarning(const std::string &msg) {
            return ResultMsg(1, msg);
        }

    }  // namespace scm
}  // namespace qifeng
