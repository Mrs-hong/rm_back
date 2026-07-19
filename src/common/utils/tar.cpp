/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/utils/tar.h"

#include "common/utils/file.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/wait.h>

namespace qifeng::scm::utils {
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
