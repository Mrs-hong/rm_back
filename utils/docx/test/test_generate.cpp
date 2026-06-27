/**
 * @file test_generate.cpp
 * @brief 测试7：从空白模板生成文档
 *
 * 使用 默认模板.docx 空白模板，通过 GenerateDocument 接口
 * 传入标题 + 正文（Plain/HTML/Markdown）生成完整文档。
 */

#include "utils/docx/docx_document.h"
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

/// 验证输出 docx 中的 document.xml 是否包含指定文本
static bool verifyOutput(const std::string& docxPath,
                         const std::string& verifyDir,
                         const std::vector<std::string>& expectedTexts) {
    fs::create_directories(verifyDir);
    if (!utils::docx::UnzipToDir(docxPath, verifyDir)) return false;

    std::string docXml = readFile(verifyDir + "/word/document.xml");
    fs::remove_all(verifyDir);

    for (const auto& text : expectedTexts) {
        if (docXml.find(text) == std::string::npos) return false;
    }
    return true;
}

int main() {
    std::cout << "=== 测试7：从空白模板生成文档 ===" << std::endl;

    std::string dataDir = TEST_DATA_DIR;
    std::string blankTemplate = dataDir + "/../../templates/默认模板.docx";
    std::string mdPath = dataDir + "/content.md";
    std::string htmlPath = dataDir + "/content.html";

    // ── 测试7.1：标题 + Markdown 正文 ──
    std::cout << std::endl << "  --- 7.1 标题 + Markdown 正文 ---" << std::endl;
    {
        std::string mdContent = readFile(mdPath);
        ASSERT_TRUE(!mdContent.empty(), "读取 content.md");

        std::string outputPath = dataDir + "/output_generate_md.docx";

        utils::docx::DocxDocument doc;
        auto openErr = doc.Open(blankTemplate);
        ASSERT_TRUE(openErr.IsOk(), "打开空白模板");

        auto result = doc.GenerateDocument("季度工作总结会议纪要", mdContent,
                                            utils::docx::ContentType::Markdown);
        ASSERT_TRUE(result.IsOk(), "生成文档(MD)成功");
        ASSERT_TRUE(result.Value().totalReplaced == 1, "生成记录为1");

        auto saveErr = doc.Save(outputPath);
        ASSERT_TRUE(saveErr.IsOk(), "保存输出文件");
        ASSERT_TRUE(fs::exists(outputPath), "输出文件存在");

        // 验证输出包含标题和正文内容
        bool ok = verifyOutput(outputPath, dataDir + "/verify_gen_md",
                               {"季度工作总结会议纪要", "会议内容", "项目进度", "仿宋", "方正小标宋简体"});
        ASSERT_TRUE(ok, "输出包含标题、正文、字体格式");

        doc.Close();
        std::cout << "  输出文件: " << outputPath << std::endl;
    }

    // ── 测试7.2：标题 + HTML 正文 ──
    std::cout << std::endl << "  --- 7.2 标题 + HTML 正文 ---" << std::endl;
    {
        std::string htmlContent = readFile(htmlPath);
        ASSERT_TRUE(!htmlContent.empty(), "读取 content.html");

        std::string outputPath = dataDir + "/output_generate_html.docx";

        utils::docx::DocxDocument doc;
        auto openErr = doc.Open(blankTemplate);
        ASSERT_TRUE(openErr.IsOk(), "打开空白模板");

        auto result = doc.GenerateDocument("项目进度汇报", htmlContent,
                                            utils::docx::ContentType::HTML);
        ASSERT_TRUE(result.IsOk(), "生成文档(HTML)成功");

        auto saveErr = doc.Save(outputPath);
        ASSERT_TRUE(saveErr.IsOk(), "保存输出文件");
        ASSERT_TRUE(fs::exists(outputPath), "输出文件存在");

        bool ok = verifyOutput(outputPath, dataDir + "/verify_gen_html",
                               {"项目进度汇报", "项目进度", "黑体"});
        ASSERT_TRUE(ok, "输出包含标题、正文、字体格式");

        doc.Close();
        std::cout << "  输出文件: " << outputPath << std::endl;
    }

    // ── 测试7.3：标题 + 纯文本正文 ──
    std::cout << std::endl << "  --- 7.3 标题 + 纯文本正文 ---" << std::endl;
    {
        std::string plainContent =
            "第一行：会议开场白，由主持人介绍会议背景与目标。\n"
            "第二行：各部门依次汇报本季度工作进展。\n"
            "第三行：讨论下阶段工作计划与资源分配。\n"
            "第四行：形成会议决议，明确责任分工。";

        std::string outputPath = dataDir + "/output_generate_plain.docx";

        utils::docx::DocxDocument doc;
        auto openErr = doc.Open(blankTemplate);
        ASSERT_TRUE(openErr.IsOk(), "打开空白模板");

        auto result = doc.GenerateDocument("会议纪要", plainContent,
                                            utils::docx::ContentType::Plain);
        ASSERT_TRUE(result.IsOk(), "生成文档(Plain)成功");

        auto saveErr = doc.Save(outputPath);
        ASSERT_TRUE(saveErr.IsOk(), "保存输出文件");
        ASSERT_TRUE(fs::exists(outputPath), "输出文件存在");

        bool ok = verifyOutput(outputPath, dataDir + "/verify_gen_plain",
                               {"会议纪要", "第一行", "第四行", "仿宋"});
        ASSERT_TRUE(ok, "输出包含标题、正文、仿宋字体");

        doc.Close();
        std::cout << "  输出文件: " << outputPath << std::endl;
    }

    // ── 测试7.4：无标题 + Markdown 正文 ──
    std::cout << std::endl << "  --- 7.4 无标题 + Markdown 正文 ---" << std::endl;
    {
        std::string mdContent = readFile(mdPath);
        std::string outputPath = dataDir + "/output_generate_notitle.docx";

        utils::docx::DocxDocument doc;
        auto openErr = doc.Open(blankTemplate);
        ASSERT_TRUE(openErr.IsOk(), "打开空白模板");

        // 标题为空
        auto result = doc.GenerateDocument("", mdContent,
                                            utils::docx::ContentType::Markdown);
        ASSERT_TRUE(result.IsOk(), "生成文档(无标题)成功");

        auto saveErr = doc.Save(outputPath);
        ASSERT_TRUE(saveErr.IsOk(), "保存输出文件");
        ASSERT_TRUE(fs::exists(outputPath), "输出文件存在");

        // 验证输出包含正文内容
        bool ok = verifyOutput(outputPath, dataDir + "/verify_gen_notitle",
                               {"会议内容", "项目进度"});
        ASSERT_TRUE(ok, "输出包含正文内容");

        doc.Close();
        std::cout << "  输出文件: " << outputPath << std::endl;
    }

    // ── 测试7.5：错误处理 ──
    std::cout << std::endl << "  --- 7.5 错误处理 ---" << std::endl;
    {
        // 未打开文档
        utils::docx::DocxDocument doc;
        auto result = doc.GenerateDocument("标题", "正文",
                                            utils::docx::ContentType::Plain);
        ASSERT_TRUE(!result.IsOk(), "未打开文档时返回错误");
        ASSERT_TRUE(result.Code() == static_cast<int>(utils::docx::ErrorCode::NotOpened),
                    "错误码为 NotOpened");

        // 空正文内容
        auto openErr = doc.Open(blankTemplate);
        ASSERT_TRUE(openErr.IsOk(), "打开空白模板(错误测试)");

        auto result2 = doc.GenerateDocument("标题", "",
                                             utils::docx::ContentType::Plain);
        ASSERT_TRUE(!result2.IsOk(), "空正文返回错误");
        ASSERT_TRUE(result2.Code() == static_cast<int>(utils::docx::ErrorCode::InvalidPattern),
                    "错误码为 InvalidPattern");

        doc.Close();
    }

    std::cout << std::endl;
    std::cout << "=== 测试结果: " << testPassCount << " passed, "
              << testFailCount << " failed ===" << std::endl;

    return testFailCount == 0 ? 0 : 1;
}
