/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/utils/symlink.h"

#include <filesystem>

namespace qifeng::scm::utils {
    ResultMsg CreateSymbolicLink(const std::string &target, const std::string &linkPath) {
        namespace fs = std::filesystem;
        if (fs::exists(linkPath)) {
            return MakeError("Symbolic link already exists: " + linkPath);
        }
        try {
            fs::create_symlink(target, linkPath);
            return MakeSuccess();
        } catch (const std::exception &e) {
            return MakeError("Failed to create symbolic link: " + std::string(e.what()));
        }
    }

    ResultMsg DeleteSymbolicLink(const std::string &linkPath) {
        namespace fs = std::filesystem;
        if (!fs::exists(linkPath)) {
            return MakeSuccess();
        }
        if (!fs::is_symlink(linkPath)) {
            return MakeError("Path is not a symbolic link: " + linkPath);
        }
        try {
            fs::remove(linkPath);
            return MakeSuccess();
        } catch (const std::exception &e) {
            return MakeError("Failed to delete symbolic link: " + std::string(e.what()));
        }
    }
}  // namespace qifeng::scm::utils
