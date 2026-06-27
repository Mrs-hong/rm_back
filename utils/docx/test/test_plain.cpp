/**
 * @file test_plain.cpp
 * @brief 测试1：纯文本批量替换
 *
 * 打开 template.docx，批量替换 6 个占位符，
 * 验证替换次数、记录完整性、输出文件可解压。
 */

#include "utils/docx/docx_document.h"
#include "zip_utils.h"

#include <iostream>
#include <fstream>
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

int main() {
    std::cout << "=== 测试1：纯文本批量替换 ===" << std::endl;

    std::string dataDir = TEST_DATA_DIR;
    std::string templatePath = dataDir + "/template.docx";
    std::string outputPath = dataDir + "/output_plain.docx";

    // 准备替换映射
    std::map<std::string, std::string> replacements;
    replacements["会议主题"] = "年终工作总结会议";
    replacements["会议时间"] = "2025年12月31日";
    replacements["会议地点"] = "总部会议室";
    replacements["主持人"] = "张三";
    replacements["参会人员"] = "全体员工";
    replacements["备注"] = "请准时参加";

    // 使用 DocxDocument class API
    utils::docx::DocxConfig config;
    config.verbose = false;
    utils::docx::DocxDocument doc(config);

    // 测试打开
    auto openErr = doc.Open(templatePath);
    ASSERT_TRUE(openErr.IsOk(), "打开模板文件");

    // 测试批量替换
    auto result = doc.ReplaceText(replacements);
    std::cout << "  替换次数: " << result.Value().totalReplaced << std::endl;
    ASSERT_TRUE(result.IsOk(), "批量替换成功");
    ASSERT_TRUE(result.Value().totalReplaced >= 1, "至少替换1处 (实际: " + std::to_string(result.Value().totalReplaced) + ")");

    // 测试保存
    auto saveErr = doc.Save(outputPath);
    ASSERT_TRUE(saveErr.IsOk(), "保存输出文件");

    // 测试输出文件存在
    ASSERT_TRUE(fs::exists(outputPath), "输出文件存在");

    // 测试输出文件可解压（验证是合法 docx）
    std::string verifyDir = dataDir + "/verify_plain";
    fs::create_directories(verifyDir);
    bool unzipOk = utils::docx::UnzipToDir(outputPath, verifyDir);
    ASSERT_TRUE(unzipOk, "输出文件可解压（合法 docx）");
    ASSERT_TRUE(fs::exists(verifyDir + "/word/document.xml"), "输出包含 word/document.xml");

    // 关闭文档（输出文件保留供人工检查）
    doc.Close();
    fs::remove_all(verifyDir);  // 仅清理解压验证目录，保留输出 docx
    std::cout << "  输出文件: " << outputPath << std::endl;

    // 测试通配符替换
    std::cout << std::endl << "  --- 通配符替换测试 ---" << std::endl;
    std::string outputPath2 = dataDir + "/output_plain_wildcard.docx";
    utils::docx::DocxDocument doc2;
    openErr = doc2.Open(templatePath);
    ASSERT_TRUE(openErr.IsOk(), "通配符: 打开模板");

    auto result2 = doc2.ReplaceText("{{*}}", "测试通配符");
    ASSERT_TRUE(result2.IsOk(), "通配符替换成功");
    ASSERT_TRUE(result2.Value().totalReplaced >= 1, "通配符至少替换1处");

    saveErr = doc2.Save(outputPath2);
    ASSERT_TRUE(saveErr.IsOk(), "通配符: 保存输出");
    doc2.Close();
    std::cout << "  输出文件: " << outputPath2 << std::endl;

    // 结果汇总
    std::cout << std::endl;
    std::cout << "=== 测试结果: " << testPassCount << " passed, "
              << testFailCount << " failed ===" << std::endl;

    return testFailCount == 0 ? 0 : 1;
}
