/**
 * @file test_error.cpp
 * @brief 测试6：错误处理
 *
 * 测试各种错误场景：文件不存在、空映射、无匹配、未打开就替换等。
 */

#include "docx_temp_helper/docx_document.h"

#include <iostream>
#include <map>

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "."
#endif

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

#define ASSERT_EQ(actual, expected, msg) \
    ASSERT_TRUE((actual) == (expected), msg)

int main() {
    std::cout << "=== 测试6：错误处理 ===" << std::endl;

    std::string dataDir = TEST_DATA_DIR;
    std::string templatePath = dataDir + "/template.docx";

    // ── 6.1 文件不存在 ──
    std::cout << std::endl << "  --- 6.1 文件不存在 ---" << std::endl;
    {
        docx_temp_helper::DocxDocument doc;
        auto err = doc.Open("/nonexistent/path/file.docx");
        ASSERT_EQ(err.code, docx_temp_helper::ErrorCode::FileNotFound,
                  "文件不存在返回 FileNotFound");
        ASSERT_TRUE(!doc.IsOpen(), "未打开时 IsOpen()=false");
    }

    // ── 6.2 空替换映射 ──
    std::cout << std::endl << "  --- 6.2 空替换映射 ---" << std::endl;
    {
        docx_temp_helper::DocxDocument doc;
        auto openErr = doc.Open(templatePath);
        ASSERT_TRUE(openErr.Ok(), "打开模板");

        std::map<std::string, std::string> empty;
        auto result = doc.ReplaceText(empty);
        ASSERT_EQ(result.error.code, docx_temp_helper::ErrorCode::InvalidPattern,
                  "空映射返回 InvalidPattern");
        doc.Close();
    }

    // ── 6.3 无匹配占位符 ──
    std::cout << std::endl << "  --- 6.3 无匹配占位符 ---" << std::endl;
    {
        docx_temp_helper::DocxDocument doc;
        auto openErr = doc.Open(templatePath);
        ASSERT_TRUE(openErr.Ok(), "打开模板");

        // 使用不存在的占位符名
        std::map<std::string, std::string> replacements;
        replacements["不存在的占位符XYZ123"] = "测试";
        auto result = doc.ReplaceText(replacements);
        ASSERT_EQ(result.error.code, docx_temp_helper::ErrorCode::NoMatchFound,
                  "无匹配返回 NoMatchFound");
        ASSERT_EQ(result.totalReplaced, 0, "替换次数为0");
        doc.Close();
    }

    // ── 6.4 未 open 就调用 ReplaceText ──
    std::cout << std::endl << "  --- 6.4 未 open 就替换 ---" << std::endl;
    {
        docx_temp_helper::DocxDocument doc;
        // 不调用 open 直接替换
        std::map<std::string, std::string> replacements;
        replacements["测试"] = "值";
        auto result = doc.ReplaceText(replacements);
        ASSERT_EQ(result.error.code, docx_temp_helper::ErrorCode::NotOpened,
                  "未 open 返回 NotOpened");
    }

    // ── 6.5 未 open 就调用 save ──
    std::cout << std::endl << "  --- 6.5 未 open 就保存 ---" << std::endl;
    {
        docx_temp_helper::DocxDocument doc;
        auto err = doc.Save("/tmp/test_output.docx");
        ASSERT_EQ(err.code, docx_temp_helper::ErrorCode::NotOpened,
                  "未 open 调用 save 返回 NotOpened");
    }

    // ── 6.6 close 后再调用 ReplaceText ──
    std::cout << std::endl << "  --- 6.6 close 后再替换 ---" << std::endl;
    {
        docx_temp_helper::DocxDocument doc;
        doc.Open(templatePath);
        doc.Close();

        std::map<std::string, std::string> replacements;
        replacements["测试"] = "值";
        auto result = doc.ReplaceText(replacements);
        ASSERT_EQ(result.error.code, docx_temp_helper::ErrorCode::NotOpened,
                  "close 后替换返回 NotOpened");
        ASSERT_TRUE(!doc.IsOpen(), "close 后 IsOpen()=false");
    }

    // ── 6.7 富文本空映射 ──
    std::cout << std::endl << "  --- 6.7 富文本空映射 ---" << std::endl;
    {
        docx_temp_helper::DocxDocument doc;
        doc.Open(templatePath);

        std::map<std::string, docx_temp_helper::RichReplacement> empty;
        auto result = doc.ReplaceRich(empty);
        ASSERT_EQ(result.error.code, docx_temp_helper::ErrorCode::InvalidPattern,
                  "富文本空映射返回 InvalidPattern");
        doc.Close();
    }

    // ── 6.8 状态查询 ──
    std::cout << std::endl << "  --- 6.8 状态查询 ---" << std::endl;
    {
        docx_temp_helper::DocxConfig config;
        config.memoryLimit = 5 * 1024 * 1024;
        docx_temp_helper::DocxDocument doc(config);

        ASSERT_TRUE(!doc.IsOpen(), "初始状态 IsOpen()=false");
        ASSERT_TRUE(!doc.IsStreamingMode(), "初始 IsStreamingMode()=false");
        ASSERT_EQ(doc.GetFileSize(), 0u, "初始 fileSize=0");

        doc.Open(templatePath);
        ASSERT_TRUE(doc.IsOpen(), "open 后 IsOpen()=true");
        ASSERT_TRUE(doc.GetFileSize() > 0, "open 后 fileSize>0");

        // 配置查询
        ASSERT_EQ(doc.Config().memoryLimit, 5 * 1024 * 1024u, "config().memoryLimit 正确");
        doc.Close();
    }

    std::cout << std::endl;
    std::cout << "=== 测试结果: " << testPassCount << " passed, "
              << testFailCount << " failed ===" << std::endl;

    return testFailCount == 0 ? 0 : 1;
}
