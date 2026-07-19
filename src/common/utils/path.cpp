/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/utils/path.h"

#include <filesystem>
#include <system_error>

namespace qifeng::scm::utils {
    std::string GetAbsolutePath(const std::string &path) {
        namespace fs = std::filesystem;
        std::error_code ec;
        auto absPath = fs::absolute(path, ec);
        if (ec) {
            return path;
        }
        return absPath.string();
    }

    ResultMsg IsSamePath(const std::string &path1, const std::string &path2) {
        namespace fs = std::filesystem;
        std::error_code ec;

        // 获取规范路径（解析符号链接、去除 . 和 ..）
        fs::path canonical1 = fs::canonical(path1, ec);
        if (ec) {
            return MakeError("Invalid path1: " + path1 + ", " + ec.message());
        }

        fs::path canonical2 = fs::canonical(path2, ec);
        if (ec) {
            return MakeError("Invalid path2: " + path2 + ", " + ec.message());
        }

        if (canonical1 == canonical2) {
            return MakeSuccess();
        }
        return MakeError("Paths are different: " + canonical1.string() + " vs " + canonical2.string());
    }

    std::string GetSingleTopLevelEntryName(const std::string &dir) {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(dir, ec)) {
            return "";
        }

        // 遍历目录，统计顶层条目数量并记录第一个条目
        std::string foundName;
        int count = 0;
        for (const auto &entry : fs::directory_iterator(dir, ec)) {
            if (ec) {
                return "";
            }
            ++count;
            if (count == 1) {
                foundName = entry.path().filename().string();
                // 模型要求是目录，若唯一条目是文件则视为非法
                if (!entry.is_directory()) {
                    return "";
                }
            } else {
                // 超过一个条目，无需继续
                return "";
            }
        }

        // 仅一个目录条目时返回其名字，否则（空目录）返回空
        return (count == 1) ? foundName : "";
    }
}  // namespace qifeng::scm::utils
