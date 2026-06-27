/**
 * @file test_zip.cpp
 * @brief 测试8：ZIP 工具函数（ZipCompress / ZipExtract）
 *
 * 测试目录压缩、单文件压缩、解压、错误处理。
 */

#include "utils/docx/docx_document.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "."
#endif

namespace fs = std::filesystem;

static int testPassCount = 0;
static int testFailCount = 0;

#define ASSERT_TRUE(cond, msg) \
    do { \
        if (cond) { \
            std::cout << "  [PASS] " << msg << std::endl; \
            testPassCount++; \
        } else { \
            std::cout << "  [FAIL] " << msg << std::endl; \
            testFailCount++; \
        } \
    } while (0)

/// 创建测试目录和文件
static void createTestDir(const std::string& dir) {
    fs::create_directories(dir + "/sub1");
    fs::create_directories(dir + "/sub2");

    // 写入测试文件
    std::ofstream(dir + "/file1.txt") << "Hello, ZIP!";
    std::ofstream(dir + "/sub1/file2.txt") << "Nested file content";
    std::ofstream(dir + "/sub2/file3.txt") << "Another nested file";
}

int main() {
    std::cout << "=== 测试8：ZIP 工具函数 ===" << std::endl;

    std::string dataDir = TEST_DATA_DIR;
    std::string workDir = dataDir + "/zip_test";
    fs::remove_all(workDir);
    fs::create_directories(workDir);

    // ── 测试8.1：目录压缩 ──
    std::cout << std::endl << "  --- 8.1 目录压缩 ---" << std::endl;
    {
        std::string srcDir = workDir + "/src_dir";
        createTestDir(srcDir);

        std::string outputDir = workDir + "/output";
        std::string zipName = "dir_test.zip";

        auto err = utils::docx::ZipCompress(srcDir, outputDir, zipName);
        ASSERT_TRUE(err.IsOk(), "目录压缩成功");
        ASSERT_TRUE(fs::exists(outputDir + "/" + zipName), "ZIP 文件存在");

        // 验证文件大小 > 0
        auto size = fs::file_size(outputDir + "/" + zipName);
        ASSERT_TRUE(size > 0, "ZIP 文件大小 > 0");

        std::cout << "  输出文件: " << outputDir << "/" << zipName
                  << " (" << size << " bytes)" << std::endl;
    }

    // ── 测试8.2：目录解压 ──
    std::cout << std::endl << "  --- 8.2 目录解压 ---" << std::endl;
    {
        std::string zipPath = workDir + "/output/dir_test.zip";
        std::string extractDir = workDir + "/extracted";

        auto err = utils::docx::ZipExtract(zipPath, extractDir);
        ASSERT_TRUE(err.IsOk(), "解压成功");
        ASSERT_TRUE(fs::exists(extractDir + "/file1.txt"), "解压后 file1.txt 存在");
        ASSERT_TRUE(fs::exists(extractDir + "/sub1/file2.txt"), "解压后 sub1/file2.txt 存在");
        ASSERT_TRUE(fs::exists(extractDir + "/sub2/file3.txt"), "解压后 sub2/file3.txt 存在");

        // 验证文件内容
        std::ifstream in(extractDir + "/file1.txt");
        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        ASSERT_TRUE(content == "Hello, ZIP!", "解压后文件内容正确");

        std::cout << "  解压目录: " << extractDir << std::endl;
    }

    // ── 测试8.3：单文件压缩 ──
    std::cout << std::endl << "  --- 8.3 单文件压缩 ---" << std::endl;
    {
        std::string filePath = workDir + "/single.txt";
        std::ofstream(filePath) << "Single file compression test.";

        std::string outputDir = workDir + "/output";
        std::string zipName = "single_test.zip";

        auto err = utils::docx::ZipCompress(filePath, outputDir, zipName);
        ASSERT_TRUE(err.IsOk(), "单文件压缩成功");
        ASSERT_TRUE(fs::exists(outputDir + "/" + zipName), "ZIP 文件存在");

        // 解压验证
        std::string extractDir = workDir + "/extracted_single";
        auto err2 = utils::docx::ZipExtract(outputDir + "/" + zipName, extractDir);
        ASSERT_TRUE(err2.IsOk(), "单文件 ZIP 解压成功");
        ASSERT_TRUE(fs::exists(extractDir + "/single.txt"), "解压后文件存在");

        std::ifstream in(extractDir + "/single.txt");
        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        ASSERT_TRUE(content == "Single file compression test.", "单文件内容正确");

        std::cout << "  输出文件: " << outputDir << "/" << zipName << std::endl;
    }

    // ── 测试8.4：压缩 docx 文件（验证兼容性）──
    std::cout << std::endl << "  --- 8.4 压缩/解压 docx 文件 ---" << std::endl;
    {
        std::string docxPath = dataDir + "/template.docx";

        std::string outputDir = workDir + "/output";
        std::string zipName = "template_copy.zip";

        // 将 docx 当作普通文件压缩
        auto err = utils::docx::ZipCompress(docxPath, outputDir, zipName);
        ASSERT_TRUE(err.IsOk(), "docx 文件压缩成功");

        // 解压
        std::string extractDir = workDir + "/extracted_docx";
        auto err2 = utils::docx::ZipExtract(outputDir + "/" + zipName, extractDir);
        ASSERT_TRUE(err2.IsOk(), "docx ZIP 解压成功");
        ASSERT_TRUE(fs::exists(extractDir + "/template.docx"), "解压后 docx 存在");

        std::cout << "  输出文件: " << outputDir << "/" << zipName << std::endl;
    }

    // ── 测试8.5：错误处理 ──
    std::cout << std::endl << "  --- 8.5 错误处理 ---" << std::endl;
    {
        // 源路径不存在
        auto err = utils::docx::ZipCompress(
            "/nonexistent/path", workDir + "/output", "error.zip");
        ASSERT_TRUE(!err.IsOk(), "源路径不存在返回错误");
        ASSERT_TRUE(err.Code() == static_cast<int>(utils::docx::ErrorCode::FileNotFound),
                    "错误码为 FileNotFound");

        // ZIP 文件不存在
        auto err2 = utils::docx::ZipExtract(
            "/nonexistent/archive.zip", workDir + "/extract_err");
        ASSERT_TRUE(!err2.IsOk(), "ZIP 不存在返回错误");
        ASSERT_TRUE(err2.Code() == static_cast<int>(utils::docx::ErrorCode::FileNotFound),
                    "错误码为 FileNotFound");
    }

    // 清理测试工作目录（保留输出的 zip 文件在 output/ 中）
    fs::remove_all(workDir + "/src_dir");
    fs::remove_all(workDir + "/extracted");
    fs::remove_all(workDir + "/extracted_single");
    fs::remove_all(workDir + "/extracted_docx");
    fs::remove_all(workDir + "/extract_err");
    fs::remove(workDir + "/single.txt");

    std::cout << std::endl;
    std::cout << "=== 测试结果: " << testPassCount << " passed, "
              << testFailCount << " failed ===" << std::endl;

    return testFailCount == 0 ? 0 : 1;
}
