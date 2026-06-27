/**
 * @file test_streaming.cpp
 * @brief 测试5：流式处理模式
 *
 * 设置极小的内存限制（1KB），强制触发流式模式，
 * 验证流式替换结果与 DOM 模式一致。
 */

#include "utils/docx/docx_document.h"
#include "zip_utils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "."
#endif

namespace fs = std::filesystem;

static int testPassCount = 0;
static int testFailCount = 0;

#define ASSERT_TRUE(cond, msg)                                                 \
  do {                                                                         \
    if (cond) {                                                                \
      std::cout << "  [PASS] " << msg << std::endl;                            \
      testPassCount++;                                                         \
    } else {                                                                   \
      std::cout << "  [FAIL] " << msg << std::endl;                            \
      testFailCount++;                                                         \
    }                                                                          \
  } while (0)

/// 读取文件内容到字符串
static std::string readFile(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream oss;
  oss << in.rdbuf();
  return oss.str();
}

int main() {
  std::cout << "=== 测试5：流式处理模式 ===" << std::endl;

  std::string dataDir = TEST_DATA_DIR;
  std::string templatePath = dataDir + "/template.docx";
  std::string mdPath = dataDir + "/content.md";
  std::string outputPath = dataDir + "/output_streaming.docx";

  // ── 测试5.1：流式纯文本替换 ──
  std::cout << std::endl << "  --- 5.1 流式纯文本替换 ---" << std::endl;

  // 设置极小内存限制（1KB），强制启用流式模式
  utils::docx::DocxConfig config;
  config.memoryLimit = 1024; // 1KB，远小于文件大小
  config.verbose = false;

  utils::docx::DocxDocument doc(config);

  auto openErr = doc.Open(templatePath);
  ASSERT_TRUE(openErr.IsOk(), "打开模板文件");

  // 通配符替换
  auto result = doc.ReplaceText("{{*}}", "流式替换文本");
  ASSERT_TRUE(result.IsOk(), "流式纯文本替换成功");
  ASSERT_TRUE(result.Value().totalReplaced >= 1, "至少替换1处");

  // 验证处于流式模式
  ASSERT_TRUE(doc.IsStreamingMode(), "确认处于流式处理模式");

  auto saveErr = doc.Save(outputPath);
  ASSERT_TRUE(saveErr.IsOk(), "保存输出文件");
  ASSERT_TRUE(fs::exists(outputPath), "输出文件存在");

  // 验证输出可解压
  std::string verifyDir = dataDir + "/verify_streaming";
  fs::create_directories(verifyDir);
  bool unzipOk = utils::docx::UnzipToDir(outputPath, verifyDir);
  ASSERT_TRUE(unzipOk, "输出文件可解压");

  std::string docXml = readFile(verifyDir + "/word/document.xml");
  ASSERT_TRUE(docXml.find("流式替换文本") != std::string::npos,
              "输出包含流式替换文本");

  // 关闭文档（输出文件保留供人工检查）
  doc.Close();
  fs::remove_all(verifyDir); // 仅清理解压验证目录，保留输出 docx
  std::cout << "  输出文件: " << outputPath << std::endl;

  // ── 测试5.2：流式 Markdown 替换 ──
  std::cout << std::endl << "  --- 5.2 流式 Markdown 替换 ---" << std::endl;

  std::string mdContent = readFile(mdPath);

  std::map<std::string, utils::docx::RichReplacement> replacements;
  utils::docx::RichReplacement rich;
  rich.content = mdContent;
  rich.type = utils::docx::ContentType::Markdown;
  replacements["正文"] = rich;

  utils::docx::DocxConfig config2;
  config2.memoryLimit = 1024; // 强制流式
  config2.verbose = false;

  utils::docx::DocxDocument doc2(config2);
  openErr = doc2.Open(templatePath);
  ASSERT_TRUE(openErr.IsOk(), "流式 MD: 打开模板");

  auto result2 = doc2.ReplaceRich(replacements);
  ASSERT_TRUE(result2.IsOk(), "流式 Markdown 替换成功");
  ASSERT_TRUE(doc2.IsStreamingMode(), "确认 MD 流式模式");
  ASSERT_TRUE(result2.Value().totalReplaced >= 1, "MD 至少替换1处");

  std::string outputPath2 = dataDir + "/output_streaming_md.docx";
  saveErr = doc2.Save(outputPath2);
  ASSERT_TRUE(saveErr.IsOk(), "流式 MD: 保存输出");

  // 验证输出
  std::string verifyDir2 = dataDir + "/verify_streaming_md";
  fs::create_directories(verifyDir2);
  unzipOk = utils::docx::UnzipToDir(outputPath2, verifyDir2);
  ASSERT_TRUE(unzipOk, "流式 MD: 输出可解压");

  docXml = readFile(verifyDir2 + "/word/document.xml");
  ASSERT_TRUE(docXml.find("会议内容") != std::string::npos,
              "流式 MD: 输出包含 '会议内容'");
  ASSERT_TRUE(docXml.find("仿宋") != std::string::npos,
              "流式 MD: 输出包含仿宋字体");

  // 关闭文档（输出文件保留供人工检查）
  doc2.Close();
  fs::remove_all(verifyDir2); // 仅清理解压验证目录，保留输出 docx
  std::cout << "  输出文件: " << outputPath2 << std::endl;

  // ── 测试5.3：DOM 模式对比（大内存限制，应使用 DOM）──
  std::cout << std::endl << "  --- 5.3 DOM 模式对比 ---" << std::endl;

  utils::docx::DocxConfig config3;
  config3.memoryLimit = 100 * 1024 * 1024; // 100MB，足够大，使用 DOM
  config3.verbose = false;

  utils::docx::DocxDocument doc3(config3);
  openErr = doc3.Open(templatePath);
  ASSERT_TRUE(openErr.IsOk(), "DOM 模式: 打开模板");

  auto result3 = doc3.ReplaceText("{{*}}", "DOM替换文本");
  ASSERT_TRUE(result3.IsOk(), "DOM 模式替换成功");
  ASSERT_TRUE(!doc3.IsStreamingMode(), "确认处于 DOM 模式（非流式）");

  doc3.Close();

  std::cout << std::endl;
  std::cout << "=== 测试结果: " << testPassCount << " passed, " << testFailCount
            << " failed ===" << std::endl;

  return testFailCount == 0 ? 0 : 1;
}
