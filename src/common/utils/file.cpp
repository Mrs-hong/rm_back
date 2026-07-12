/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/utils/file.h"
#include "common/types.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/wait.h>
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

    ResultMsg GetAllFilesInDir(std::vector<std::string> &outResult, const std::string &dir, const std::string &suffix) {
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
            outResult.push_back(fs::absolute(entry.path(), ec).string());
        }

        return MakeSuccess();
    }

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

    ResultMsg ExtractTar(const std::string &tarPath, const std::string &extractDir) {
        namespace fs = std::filesystem;
        if (!fs::exists(tarPath)) {
            return MakeError("Tar file does not exist: " + tarPath);
        }
        // 检查是否为文件
        if (!fs::is_regular_file(tarPath)) {
            return MakeError("Tar file is not a regular file: " + tarPath);
        }

        if (auto result = CreateDirectory(extractDir); !result.IsDefaultSuccess()) {
            return result;
        }

        std::string cmd = "tar -xzf \"" + tarPath + "\" -C \"" + extractDir + "\" 2>&1";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            return MakeError("Failed to execute tar command");
        }

        std::array<char, 256> buffer {};
        std::string output;
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            output += buffer.data();
        }

        int status = pclose(pipe);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            return MakeError("Failed to extract tar (exit code: " +
                             std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : -1) + "): " + output);
        }
        return MakeSuccess();
    }

    ResultMsg CompressDirToTar(const std::string &dir, const std::string &tarPath) {
        namespace fs = std::filesystem;
        if (!fs::exists(dir)) {
            return MakeError("Directory does not exist: " + dir);
        }

        fs::path dirPath(dir);
        fs::path parentPath = dirPath.parent_path();
        std::string dirName = dirPath.filename().string();

        std::string cmd = "tar -czf \"" + tarPath + "\" -C \"" + parentPath.string() + "\" \"" + dirName + "\" 2>&1";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            return MakeError("Failed to execute tar command");
        }

        std::array<char, 256> buffer {};
        std::string output;
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            output += buffer.data();
        }

        int status = pclose(pipe);
        if (status != 0) {
            return MakeError("Failed to compress directory: " + output);
        }
        return MakeSuccess();
    }

    ResultMsg VerifyTarWithSha256(const std::string &tarPath, const std::string &sha256Path) {
        namespace fs = std::filesystem;
        if (!fs::exists(tarPath)) {
            return MakeError("Tar file does not exist: " + tarPath);
        }
        if (!fs::exists(sha256Path)) {
            return MakeError("SHA256 file does not exist: " + sha256Path);
        }

        std::ifstream shaFile(sha256Path);
        if (!shaFile.is_open()) {
            return MakeError("Failed to open SHA256 file: " + sha256Path);
        }

        std::string expectedHash;
        std::getline(shaFile, expectedHash);
        shaFile.close();

        size_t spacePos = expectedHash.find(' ');
        if (spacePos != std::string::npos) {
            expectedHash = expectedHash.substr(0, spacePos);
        }

        std::string cmd = "sha256sum \"" + tarPath + "\" 2>&1";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            return MakeError("Failed to execute sha256sum command");
        }

        std::array<char, 256> buffer {};
        std::string output;
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            output += buffer.data();
        }

        int status = pclose(pipe);
        if (status != 0) {
            return MakeError("Failed to compute SHA256: " + output);
        }

        spacePos = output.find(' ');
        std::string actualHash = (spacePos != std::string::npos) ? output.substr(0, spacePos) : output;

        if (actualHash != expectedHash) {
            return MakeError("SHA256 mismatch: expected " + expectedHash + ", got " + actualHash);
        }
        return MakeSuccess();
    }
}  // namespace qifeng::scm::utils
