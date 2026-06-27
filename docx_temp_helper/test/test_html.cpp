/**
 * @file test_html.cpp
 * @brief 测试3：HTML 富文本替换
 *
 * 读取 content.html，用 HTML 替换 {{正文}} 占位符，
 * 验证替换成功且输出 docx 包含加粗标记。
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
    std::cout << "=== 测试3：HTML 富文本替换 ===" << std::endl;

    std::string dataDir = TEST_DATA_DIR;
    std::string templatePath = dataDir + "/template.docx";
    std::string htmlPath = dataDir + "/content.html";
    std::string outputPath = dataDir + "/output_html.docx";

    // 读取 HTML 内容
    std::string htmlContent = readFile(htmlPath);
    ASSERT_TRUE(!htmlContent.empty(), "读取 content.html");

    // 构建 HTML 替换映射
    std::map<std::string, docx_temp_helper::RichReplacement> replacements;
    docx_temp_helper::RichReplacement rich;
    rich.content = htmlContent;
    rich.type = docx_temp_helper::ContentType::HTML;
    replacements["正文"] = rich;

    // 使用 DocxDocument class API
    docx_temp_helper::DocxConfig config;
    config.verbose = false;
    docx_temp_helper::DocxDocument doc(config);

    auto openErr = doc.Open(templatePath);
    ASSERT_TRUE(openErr.Ok(), "打开模板文件");

    auto result = doc.ReplaceRich(replacements);
    std::cout << "  替换次数: " << result.totalReplaced << std::endl;
    ASSERT_TRUE(result.Ok(), "HTML 替换成功");
    ASSERT_TRUE(result.totalReplaced >= 1, "至少替换1处");

    auto saveErr = doc.Save(outputPath);
    ASSERT_TRUE(saveErr.Ok(), "保存输出文件");
    ASSERT_TRUE(fs::exists(outputPath), "输出文件存在");

    // 解压输出，验证包含 HTML 渲染后的标记
    std::string verifyDir = dataDir + "/verify_html";
    fs::create_directories(verifyDir);
    bool unzipOk = docx_temp_helper::UnzipToDir(outputPath, verifyDir);
    ASSERT_TRUE(unzipOk, "输出文件可解压");

    // 读取 document.xml 检查格式化标记
    std::string docXmlPath = verifyDir + "/word/document.xml";
    std::string docXml = readFile(docXmlPath);

    // HTML 中 <b>项目进度</b> 应转为 <w:b/> 加粗标记
    ASSERT_TRUE(docXml.find("<w:b/>") != std::string::npos ||
                docXml.find("<w:b />") != std::string::npos ||
                docXml.find("<w:b></w:b>") != std::string::npos,
                "输出包含 <w:b/> 加粗标记");

    // HTML 中 <h2> 应转为黑体
    ASSERT_TRUE(docXml.find("黑体") != std::string::npos, "输出包含黑体字体 (h2)");

    // HTML 中应包含 "项目进度" 文本
    ASSERT_TRUE(docXml.find("项目进度") != std::string::npos, "输出包含 HTML 文本 '项目进度'");

    // 关闭文档（输出文件保留供人工检查）
    doc.Close();
    fs::remove_all(verifyDir);  // 仅清理解压验证目录，保留输出 docx
    std::cout << "  输出文件: " << outputPath << std::endl;

    std::cout << std::endl;
    std::cout << "=== 测试结果: " << testPassCount << " passed, "
              << testFailCount << " failed ===" << std::endl;

    return testFailCount == 0 ? 0 : 1;
}
