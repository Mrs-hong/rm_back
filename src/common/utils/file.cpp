/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/utils/file.h"

#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace qifeng::scm::utils {
    ResultMsg CreateDirectory(const std::string &dir) {
        namespace fs = std::filesystem;
        try {
            fs::path p = fs::path(dir).lexically_normal();
            if (!fs::exists(p)) {
                fs::create_directories(p);
            }
            return MakeSuccess();
        } catch (const std::exception &e) {
            return MakeError("Failed to create directory: " + std::string(e.what()));
        }
    }

    ResultMsg ForceDeleteDirectory(const std::string &dir) {
        namespace fs = std::filesystem;
        if (!fs::exists(dir)) {
            return MakeSuccess();
        }
        try {
            fs::remove_all(dir);
            return MakeSuccess();
        } catch (const std::exception &e) {
            return MakeError("Failed to delete directory: " + std::string(e.what()));
        }
    }

    ResultMsg MoveDirectory(const std::string &src, const std::string &dst) {
        namespace fs = std::filesystem;
        if (!fs::exists(src)) {
            return MakeError("Source directory does not exist");
        }
        try {
            fs::rename(src, dst);
            return MakeSuccess();
        } catch (const std::exception &e) {
            return MakeError("Failed to move directory: " + std::string(e.what()));
        }
    }

    ResultMsg CopyDirectory(const std::string &src, const std::string &dst) {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(src, ec)) {
            return MakeError("Source directory does not exist: " + src);
        }

        fs::create_directories(dst, ec);
        if (ec) {
            return MakeError("Failed to create destination directory: " + dst + ", " + ec.message());
        }

        for (const auto &entry : fs::directory_iterator(src)) {
            const auto &path = entry.path();
            fs::path destPath = fs::path(dst) / path.filename();

            if (entry.is_directory()) {
                auto result = CopyDirectory(path.string(), destPath.string());
                if (!result.IsDefaultSuccess()) {
                    return result;
                }
            } else {
                fs::copy_file(path, destPath, fs::copy_options::overwrite_existing, ec);
                if (ec) {
                    return MakeError("Failed to copy file: " + path.string() + " -> " + destPath.string() + ", " +
                                     ec.message());
                }
            }
        }
        return MakeSuccess();
    }

    ResultMsg ClearDirectoryContents(const std::string &dir) {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(dir, ec)) {
            return MakeSuccess();
        }

        for (const auto &entry : fs::directory_iterator(dir)) {
            fs::remove_all(entry.path(), ec);
            if (ec) {
                return MakeError("Failed to remove: " + entry.path().string() + ", " + ec.message());
            }
        }
        return MakeSuccess();
    }

    ResultMsg RemoveFile(const std::string &filePath) {
        namespace fs = std::filesystem;
        if (!fs::exists(filePath)) {
            return MakeSuccess();
        }
        try {
            fs::remove(filePath);
            return MakeSuccess();
        } catch (const std::exception &e) {
            return MakeError("Failed to remove file: " + std::string(e.what()));
        }
    }

    ResultMsg GetAllFilesInDir(std::vector<std::string> &outResul, const std::string &dir, const std::string &suffix) {
        namespace fs = std::filesystem;

        std::error_code ec;
        if (!fs::is_directory(dir, ec) || ec) {
            return MakeError("Path is not a valid directory: " + dir);
        }

        for (const auto &entry : fs::directory_iterator(dir, ec)) {
            if (ec) {
                return MakeError("Failed to iterate directory: " + dir + ", " + ec.message());
            }
            if (!entry.is_regular_file(ec) || ec) {
                continue;
            }
            const std::string filename = entry.path().filename().string();
            if (!suffix.empty() && filename.size() >= suffix.size()) {
                if (filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) != 0) {
                    continue;
                }
            } else if (!suffix.empty()) {
                continue;
            }
            outResul.push_back(fs::absolute(entry.path(), ec).string());
        }

        return MakeSuccess();
    }
}  // namespace qifeng::scm::utils
