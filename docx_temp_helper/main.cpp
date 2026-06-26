/**
 * @file main.cpp
 * @brief test_docx 测试程序入口
 *
 * 用法1（纯文本替换）：
 *   test_docx <输入docx> <模式> <替换文本>
 *
 * 示例1：
 *   test_docx ./联合纪要说明.docx "{{*}}" "小红有才"
 *
 * 用法2（富文本替换 - Markdown）：
 *   test_docx <输入docx> --md <占位符名> <md内容> [输出docx]
 *
 * 示例2：
 *   test_docx ./联合纪要说明.docx --md "正文" "# 标题\n这是**加粗**和*斜体*"
 *
 * 用法3（富文本替换 - HTML）：
 *   test_docx <输入docx> --html <占位符名> <html内容> [输出docx]
 *
 * 示例3：
 *   test_docx ./联合纪要说明.docx --html "正文" "<h2>标题</h2><p>这是<b>加粗</b></p>"
 */

#include "docx_temp_helper/docx_document.h"

#include <iostream>
#include <string>

/// 生成默认输出文件名
static std::string defaultOutputPath(const std::string& inputPath) {
    std::string outputPath = inputPath;
    size_t dotPos = outputPath.rfind('.');
    if (dotPos != std::string::npos) {
        outputPath = outputPath.substr(0, dotPos) + "_replaced.docx";
    } else {
        outputPath += "_replaced.docx";
    }
    return outputPath;
}

/// 打印替换结果
static void printResult(const docx_temp_helper::ReplaceResult& result,
                         const std::string& outputPath) {
    if (result.ok()) {
        std::cout << "=== 替换成功 ===" << std::endl;
        std::cout << "总替换次数: " << result.totalReplaced << std::endl;
        std::cout << "替换明细:" << std::endl;
        for (const auto& rec : result.records) {
            std::cout << "  " << rec.placeholder
                      << " -> " << rec.replacement
                      << " (x" << rec.count << ")" << std::endl;
        }
        std::cout << std::endl;
        std::cout << "输出文件: " << outputPath << std::endl;
    } else {
        std::cerr << "=== 替换失败 ===" << std::endl;
        std::cerr << result.error.toString() << std::endl;
    }
}

int main(int argc, char* argv[]) {
    // ── 用法1：纯文本替换 ──
    // test_docx <输入docx> <模式> <替换文本>
    if (argc == 4 && std::string(argv[2]) != "--md" && std::string(argv[2]) != "--html") {
        std::string inputPath = argv[1];
        std::string pattern = argv[2];
        std::string replacement = argv[3];
        std::string outputPath = defaultOutputPath(inputPath);

        std::cout << "=== DOCX 纯文本占位符替换 ===" << std::endl;
        std::cout << "输入文件: " << inputPath << std::endl;
        std::cout << "匹配模式: " << pattern << std::endl;
        std::cout << "替换文本: " << replacement << std::endl;
        std::cout << "输出文件: " << outputPath << std::endl;
        std::cout << std::endl;

        // 使用 DocxDocument class API
        docx_temp_helper::DocxConfig config;
        config.verbose = true;
        docx_temp_helper::DocxDocument doc(config);

        auto openErr = doc.open(inputPath);
        if (!openErr.ok()) {
            std::cerr << "=== 打开失败 ===" << std::endl;
            std::cerr << openErr.toString() << std::endl;
            return 1;
        }

        auto result = doc.replaceText(pattern, replacement);
        if (result.ok()) {
            auto saveErr = doc.save(outputPath);
            if (!saveErr.ok()) {
                std::cerr << "=== 保存失败 ===" << std::endl;
                std::cerr << saveErr.toString() << std::endl;
                doc.close();
                return 1;
            }
            printResult(result, outputPath);
        } else {
            printResult(result, outputPath);
        }

        doc.close();
        return result.ok() ? 0 : 1;
    }

    // ── 用法2/3：富文本替换 ──
    // test_docx <输入docx> --md <占位符名> <内容> [输出docx]
    // test_docx <输入docx> --html <占位符名> <内容> [输出docx]
    if (argc >= 5 && (std::string(argv[2]) == "--md" || std::string(argv[2]) == "--html")) {
        std::string inputPath = argv[1];
        bool isHtml = (std::string(argv[2]) == "--html");
        std::string placeholderName = argv[3];
        std::string content = argv[4];
        std::string outputPath = (argc >= 6) ? argv[5] : defaultOutputPath(inputPath);

        std::cout << "=== DOCX 富文本占位符替换 ===" << std::endl;
        std::cout << "输入文件: " << inputPath << std::endl;
        std::cout << "占位符: {{" << placeholderName << "}}" << std::endl;
        std::cout << "内容类型: " << (isHtml ? "HTML" : "Markdown") << std::endl;
        std::cout << "内容预览: " << content.substr(0, 80) << (content.size() > 80 ? "..." : "") << std::endl;
        std::cout << "输出文件: " << outputPath << std::endl;
        std::cout << std::endl;

        // 构建富文本替换映射
        std::map<std::string, docx_temp_helper::RichReplacement> replacements;
        docx_temp_helper::RichReplacement rich;
        rich.content = content;
        rich.type = isHtml ? docx_temp_helper::ContentType::HTML
                           : docx_temp_helper::ContentType::Markdown;
        replacements[placeholderName] = rich;

        // 使用 DocxDocument class API
        docx_temp_helper::DocxConfig config;
        config.verbose = true;
        docx_temp_helper::DocxDocument doc(config);

        auto openErr = doc.open(inputPath);
        if (!openErr.ok()) {
            std::cerr << "=== 打开失败 ===" << std::endl;
            std::cerr << openErr.toString() << std::endl;
            return 1;
        }

        auto result = doc.replaceRich(replacements);
        if (result.ok()) {
            auto saveErr = doc.save(outputPath);
            if (!saveErr.ok()) {
                std::cerr << "=== 保存失败 ===" << std::endl;
                std::cerr << saveErr.toString() << std::endl;
                doc.close();
                return 1;
            }
            printResult(result, outputPath);
        } else {
            printResult(result, outputPath);
        }

        doc.close();
        return result.ok() ? 0 : 1;
    }

    // ── 用法说明 ──
    std::cerr << "用法:" << std::endl;
    std::cerr << "  纯文本: " << argv[0] << " <输入docx> <模式> <替换文本>" << std::endl;
    std::cerr << "  Markdown: " << argv[0] << " <输入docx> --md <占位符名> <md内容> [输出docx]" << std::endl;
    std::cerr << "  HTML: " << argv[0] << " <输入docx> --html <占位符名> <html内容> [输出docx]" << std::endl;
    std::cerr << std::endl;
    std::cerr << "示例:" << std::endl;
    std::cerr << "  " << argv[0] << " ./联合纪要说明.docx \"{{*}}\" \"小红有才\"" << std::endl;
    std::cerr << "  " << argv[0] << " ./联合纪要说明.docx --md \"正文\" \"# 标题\\n这是正文\"" << std::endl;
    std::cerr << "  " << argv[0] << " ./联合纪要说明.docx --html \"正文\" \"<h2>标题</h2><p>正文</p>\"" << std::endl;
    return 1;
}
