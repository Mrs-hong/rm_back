/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/utils/string.h"

namespace qifeng::scm::utils {
    bool HasSuffix(const std::string &str, const std::string &suffix) {
        return str.size() >= suffix.size() &&
               str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    }
}  // namespace qifeng::scm::utils
