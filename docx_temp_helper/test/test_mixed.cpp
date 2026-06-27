/**
 * @file test_mixed.cpp
 * @brief 测试4：混合替换（纯文本 + Markdown）
 *
 * 同时替换纯文本占位符和 Markdown 富文本占位符，
 * 验证两阶段替换（富文本先于纯文本）正确工作。
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
    std::cout << "=== 测试4：混合替换（纯文本 + Markdown）===" << std::endl;

    std::string dataDir = TEST_DATA_DIR;
    std::string templatePath = dataDir + "/template.docx";
    std::string mdPath = dataDir + "/content.md";
    std::string outputPath = dataDir + "/output_mixed.docx";

    // 读取 Markdown 内容
    std::string mdContent = readFile(mdPath);

    // 构建混合替换映射
    std::map<std::string, docx_temp_helper::RichReplacement> replacements;

    // 纯文本替换项
    docx_temp_helper::RichReplacement plainTopic;
    plainTopic.content = "年度总结大会";
    plainTopic.type = docx_temp_helper::ContentType::Plain;
    replacements["会议主题"] = plainTopic;

    docx_temp_helper::RichReplacement plainHost;
    plainHost.content = "李四";
    plainHost.type = docx_temp_helper::ContentType::Plain;
    replacements["主持人"] = plainHost;

    // Markdown 富文本替换项
    docx_temp_helper::RichReplacement richBody;
    richBody.content = mdContent;
    richBody.type = docx_temp_helper::ContentType::Markdown;
    replacements["正文"] = richBody;

    // 使用 DocxDocument class API
    docx_temp_helper::DocxConfig config;
    config.verbose = false;
    docx_temp_helper::DocxDocument doc(config);

    auto openErr = doc.Open(templatePath);
    ASSERT_TRUE(openErr.Ok(), "打开模板文件");

    auto result = doc.ReplaceRich(replacements);
    std::cout << "  替换次数: " << result.totalReplaced << std::endl;
    ASSERT_TRUE(result.Ok(), "混合替换成功");
    ASSERT_TRUE(result.totalReplaced >= 2, "至少替换2处 (纯文本+富文本)");

    auto saveErr = doc.Save(outputPath);
    ASSERT_TRUE(saveErr.Ok(), "保存输出文件");
    ASSERT_TRUE(fs::exists(outputPath), "输出文件存在");

    // 解压验证
    std::string verifyDir = dataDir + "/verify_mixed";
    fs::create_directories(verifyDir);
    bool unzipOk = docx_temp_helper::UnzipToDir(outputPath, verifyDir);
    ASSERT_TRUE(unzipOk, "输出文件可解压");

    std::string docXml = readFile(verifyDir + "/word/document.xml");

    // 验证纯文本替换
    ASSERT_TRUE(docXml.find("年度总结大会") != std::string::npos, "输出包含纯文本 '年度总结大会'");
    ASSERT_TRUE(docXml.find("李四") != std::string::npos, "输出包含纯文本 '李四'");

    // 验证 Markdown 替换
    ASSERT_TRUE(docXml.find("会议内容") != std::string::npos, "输出包含 MD 文本 '会议内容'");
    ASSERT_TRUE(docXml.find("项目进度") != std::string::npos, "输出包含 MD 文本 '项目进度'");

    // 关闭文档（输出文件保留供人工检查）
    doc.Close();
    fs::remove_all(verifyDir);  // 仅清理解压验证目录，保留输出 docx
    std::cout << "  输出文件: " << outputPath << std::endl;

    std::cout << std::endl;
    std::cout << "=== 测试结果: " << testPassCount << " passed, "
              << testFailCount << " failed ===" << std::endl;

    return testFailCount == 0 ? 0 : 1;
}
