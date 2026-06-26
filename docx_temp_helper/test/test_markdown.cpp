/**
 * @file test_markdown.cpp
 * @brief 测试2：Markdown 富文本替换
 *
 * 读取 content.md，用 Markdown 替换 {{正文}} 占位符，
 * 验证替换成功且输出 docx 包含 MD 中的文本。
 */

#include "docx_temp_helper/docx_document.h"
#include "zip_utils.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <map>

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

/// 读取文件内容到字符串
static std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

int main() {
    std::cout << "=== 测试2：Markdown 富文本替换 ===" << std::endl;

    std::string dataDir = TEST_DATA_DIR;
    std::string templatePath = dataDir + "/template.docx";
    std::string mdPath = dataDir + "/content.md";
    std::string outputPath = dataDir + "/output_md.docx";

    // 读取 Markdown 内容
    std::string mdContent = readFile(mdPath);
    ASSERT_TRUE(!mdContent.empty(), "读取 content.md");

    // 构建 Markdown 替换映射
    std::map<std::string, docx_temp_helper::RichReplacement> replacements;
    docx_temp_helper::RichReplacement rich;
    rich.content = mdContent;
    rich.type = docx_temp_helper::ContentType::Markdown;
    replacements["正文"] = rich;

    // 使用 DocxDocument class API
    docx_temp_helper::DocxConfig config;
    config.verbose = false;
    docx_temp_helper::DocxDocument doc(config);

    auto openErr = doc.open(templatePath);
    ASSERT_TRUE(openErr.ok(), "打开模板文件");

    auto result = doc.replaceRich(replacements);
    std::cout << "  替换次数: " << result.totalReplaced << std::endl;
    ASSERT_TRUE(result.ok(), "Markdown 替换成功");
    ASSERT_TRUE(result.totalReplaced >= 1, "至少替换1处");

    auto saveErr = doc.save(outputPath);
    ASSERT_TRUE(saveErr.ok(), "保存输出文件");
    ASSERT_TRUE(fs::exists(outputPath), "输出文件存在");

    // 解压输出，验证包含 MD 中的文本
    std::string verifyDir = dataDir + "/verify_md";
    fs::create_directories(verifyDir);
    bool unzipOk = docx_temp_helper::unzipToDir(outputPath, verifyDir);
    ASSERT_TRUE(unzipOk, "输出文件可解压");

    // 读取 document.xml 检查是否包含 MD 中的文本
    std::string docXmlPath = verifyDir + "/word/document.xml";
    std::string docXml = readFile(docXmlPath);
    ASSERT_TRUE(docXml.find("会议内容") != std::string::npos, "输出包含 MD 文本 '会议内容'");
    ASSERT_TRUE(docXml.find("项目进度") != std::string::npos, "输出包含 MD 文本 '项目进度'");

    // 检查是否包含仿宋字体（GB/T 9704-2012 格式）
    ASSERT_TRUE(docXml.find("仿宋") != std::string::npos, "输出包含仿宋字体格式");

    // 清理
    doc.close();
    fs::remove_all(verifyDir);
    fs::remove(outputPath);

    std::cout << std::endl;
    std::cout << "=== 测试结果: " << testPassCount << " passed, "
              << testFailCount << " failed ===" << std::endl;

    return testFailCount == 0 ? 0 : 1;
}
