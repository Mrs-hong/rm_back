/**
 * @file zip_utils.cpp
 * @brief ZIP 工具实现：基于 minizip 的解压/压缩功能
 */

#include "zip_utils.h"

#include <zip.h>
#include <unzip.h>
#include <ioapi.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <filesystem>
#include <vector>
#include <string>

namespace fs = std::filesystem;

namespace docx_temp_helper {

// ───────── 辅助函数 ─────────

/// 确保目录存在，递归创建
static bool EnsureDir(const std::string& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    return !ec;
}

/// 从 ZIP 内的文件路径提取目录部分并创建
static bool EnsureParentDir(const std::string& filePath) {
    fs::path p(filePath);
    fs::path parent = p.parent_path();
    if (parent.empty()) return true;
    return EnsureDir(parent.string());
}

// ───────── 解压实现 ─────────

bool UnzipToDir(const std::string& zipPath, const std::string& destDir) {
    // 打开 ZIP 文件
    unzFile uf = unzOpen(zipPath.c_str());
    if (uf == nullptr) {
        return false;
    }

    // 确保目标目录存在
    if (!EnsureDir(destDir)) {
        unzClose(uf);
        return false;
    }

    bool success = true;

    // 获取 ZIP 中第一个文件
    if (unzGoToFirstFile(uf) != UNZ_OK) {
        unzClose(uf);
        return false;
    }

    do {
        // 获取当前文件信息
        unz_file_info fileInfo;
        char fileName[4096];
        if (unzGetCurrentFileInfo(uf, &fileInfo, fileName, sizeof(fileName),
                                   nullptr, 0, nullptr, 0) != UNZ_OK) {
            success = false;
            break;
        }

        // 构造完整的输出路径
        std::string fullPath = destDir + "/" + fileName;

        // 如果是目录（路径以 / 结尾），创建目录并跳过
        if (fileName[strlen(fileName) - 1] == '/') {
            EnsureDir(fullPath);
            continue;
        }

        // 确保父目录存在
        if (!EnsureParentDir(fullPath)) {
            success = false;
            break;
        }

        // 打开当前文件的数据流
        if (unzOpenCurrentFile(uf) != UNZ_OK) {
            success = false;
            break;
        }

        // 创建输出文件
        FILE* outFile = fopen(fullPath.c_str(), "wb");
        if (outFile == nullptr) {
            unzCloseCurrentFile(uf);
            success = false;
            break;
        }

        // 读取并写入数据
        constexpr int BUFFER_SIZE = 8192;
        char buffer[BUFFER_SIZE];
        int readBytes;
        while ((readBytes = unzReadCurrentFile(uf, buffer, BUFFER_SIZE)) > 0) {
            if (fwrite(buffer, 1, static_cast<size_t>(readBytes), outFile)
                != static_cast<size_t>(readBytes)) {
                success = false;
                break;
            }
        }

        fclose(outFile);
        unzCloseCurrentFile(uf);

        if (readBytes < 0) {
            success = false;
            break;
        }

    } while (unzGoToNextFile(uf) == UNZ_OK);

    unzClose(uf);
    return success;
}

// ───────── 压缩实现 ─────────

/// 递归收集目录下所有文件的相对路径
static void CollectFiles(const fs::path& baseDir,
                          const fs::path& currentDir,
                          std::vector<std::pair<std::string, std::string>>& outFiles) {
    for (const auto& entry : fs::directory_iterator(currentDir)) {
        if (entry.is_directory()) {
            CollectFiles(baseDir, entry.path(), outFiles);
        } else if (entry.is_regular_file()) {
            // 相对于 baseDir 的路径（用于 ZIP 内的路径名）
            std::string relative = fs::relative(entry.path(), baseDir).string();
            // 统一使用正斜杠（ZIP 标准分隔符）
            for (auto& c : relative) {
                if (c == '\\') c = '/';
            }
            outFiles.emplace_back(relative, entry.path().string());
        }
    }
}

bool ZipDir(const std::string& srcDir, const std::string& zipPath,
            bool storeOnly) {
    // 打开输出 ZIP 文件
    zipFile zf = zipOpen(zipPath.c_str(), APPEND_STATUS_CREATE);
    if (zf == nullptr) {
        return false;
    }

    // 收集所有文件（相对路径, 绝对路径）
    std::vector<std::pair<std::string, std::string>> files;
    CollectFiles(srcDir, srcDir, files);

    // 压缩方法与级别：storeOnly 时使用 Store（method=0），否则使用 Deflate
    const int method = storeOnly ? 0 : Z_DEFLATED;
    const int level = storeOnly ? 0 : Z_DEFAULT_COMPRESSION;

    bool success = true;
    constexpr int BUFFER_SIZE = 8192;
    char buffer[BUFFER_SIZE];

    for (const auto& [relPath, absPath] : files) {
        // 打开要压缩的文件
        FILE* inFile = fopen(absPath.c_str(), "rb");
        if (inFile == nullptr) {
            success = false;
            continue;
        }

        // 在 ZIP 中创建新条目
        zip_fileinfo zi;
        memset(&zi, 0, sizeof(zi));
        if (zipOpenNewFileInZip(zf, relPath.c_str(), &zi,
                                 nullptr, 0, nullptr, 0, nullptr,
                                 method, level) != ZIP_OK) {
            fclose(inFile);
            success = false;
            continue;
        }

        // 读取文件内容并写入 ZIP
        size_t readBytes;
        while ((readBytes = fread(buffer, 1, BUFFER_SIZE, inFile)) > 0) {
            if (zipWriteInFileInZip(zf, buffer, static_cast<unsigned int>(readBytes)) != ZIP_OK) {
                success = false;
                break;
            }
        }

        fclose(inFile);
        zipCloseFileInZip(zf);
    }

    zipClose(zf, nullptr);
    return success;
}

// ───────── 单文件压缩实现 ─────────

bool ZipSingleFile(const std::string& filePath, const std::string& zipPath,
                   bool storeOnly) {
    zipFile zf = zipOpen(zipPath.c_str(), APPEND_STATUS_CREATE);
    if (zf == nullptr) {
        return false;
    }

    // 压缩方法与级别：storeOnly 时使用 Store（method=0），否则使用 Deflate
    const int method = storeOnly ? 0 : Z_DEFLATED;
    const int level = storeOnly ? 0 : Z_DEFAULT_COMPRESSION;

    bool success = true;
    constexpr int BUFFER_SIZE = 8192;
    char buffer[BUFFER_SIZE];

    FILE* inFile = fopen(filePath.c_str(), "rb");
    if (inFile == nullptr) {
        zipClose(zf, nullptr);
        return false;
    }

    // ZIP 内文件名：取 basename（不含目录路径）
    fs::path p(filePath);
    std::string entryName = p.filename().string();

    zip_fileinfo zi;
    memset(&zi, 0, sizeof(zi));
    if (zipOpenNewFileInZip(zf, entryName.c_str(), &zi,
                             nullptr, 0, nullptr, 0, nullptr,
                             method, level) != ZIP_OK) {
        fclose(inFile);
        zipClose(zf, nullptr);
        return false;
    }

    size_t readBytes;
    while ((readBytes = fread(buffer, 1, BUFFER_SIZE, inFile)) > 0) {
        if (zipWriteInFileInZip(zf, buffer, static_cast<unsigned int>(readBytes)) != ZIP_OK) {
            success = false;
            break;
        }
    }

    fclose(inFile);
    zipCloseFileInZip(zf);
    zipClose(zf, nullptr);
    return success;
}

} // namespace docx_temp_helper
