/**
 * @file docx_replacer.cpp
 * @brief DOCX 模板占位符替换 - 主实现
 *
 * 核心算法：跨 Run 占位符替换（逐字符法）
 *
 * OOXML 中 {{会议主题}} 通常被 Word 拆分为 3 个 <w:r> 元素：
 *   <w:r><w:rPr>样式A</w:rPr><w:t>{{</w:t></w:r>
 *   <w:r><w:rPr>样式B</w:rPr><w:t>会议主题</w:t></w:r>
 *   <w:r><w:rPr>样式A</w:rPr><w:t>}}</w:t></w:r>
 *
 * 替换策略：逐字符遍历每个 run 的文本：
 *   - 匹配区间 [startPos, endPos) 内的字符跳过（属于 {{name}}）
 *   - 在 startPos 位置插入替换文本
 *   - 匹配区间外的字符保留原样
 *   - 所有 <w:r> / <w:rPr> 节点不动，仅修改 <w:t> 文本
 */

#include "docx_temp_helper/docx_replacer.h"
#include "docx_temp_helper/rich_content.h"
#include "zip_utils.h"
#include "xml_utils.h"

#include <pugixml.hpp>

#include <iostream>
#include <fstream>
#include <filesystem>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <functional>
#include <map>

namespace fs = std::filesystem;

namespace docx_temp_helper {

// ───────── 辅助函数 ─────────

/// ErrorInfo::toString() 实现
std::string ErrorInfo::toString() const {
    std::string result = "[";

    // 错误码转字符串
    switch (code) {
        case ErrorCode::Ok:                 result += "Ok"; break;
        case ErrorCode::FileNotFound:       result += "FileNotFound"; break;
        case ErrorCode::FileNotReadable:    result += "FileNotReadable"; break;
        case ErrorCode::InvalidDocxFormat:  result += "InvalidDocxFormat"; break;
        case ErrorCode::UnzipFailed:        result += "UnzipFailed"; break;
        case ErrorCode::XmlParseFailed:     result += "XmlParseFailed"; break;
        case ErrorCode::XmlSaveFailed:      result += "XmlSaveFailed"; break;
        case ErrorCode::ZipFailed:          result += "ZipFailed"; break;
        case ErrorCode::TempDirCreateFailed: result += "TempDirCreateFailed"; break;
        case ErrorCode::NoMatchFound:       result += "NoMatchFound"; break;
        case ErrorCode::InvalidPattern:     result += "InvalidPattern"; break;
        case ErrorCode::OutputWriteFailed:  result += "OutputWriteFailed"; break;
        case ErrorCode::UnknownError:       result += "UnknownError"; break;
    }
    result += "] ";

    if (!message.empty()) {
        result += message;
    }
    if (!detail.empty()) {
        result += " | " + detail;
    }
    return result;
}

/// 创建错误信息
static ErrorInfo makeError(ErrorCode code, const std::string& message,
                            const std::string& detail = "") {
    ErrorInfo err;
    err.code = code;
    err.message = message;
    err.detail = detail;
    return err;
}

/// 创建唯一临时目录
static std::string createTempDir(const std::string& baseDir) {
    // 生成唯一目录名：docx_<pid>_<随机数>
    std::string tempName = "docx_" + std::to_string(getpid()) + "_XXXXXX";

    // 使用 mkdtemp 创建唯一目录
    char tempPath[256];
    if (!baseDir.empty()) {
        snprintf(tempPath, sizeof(tempPath), "%s/%s",
                 baseDir.c_str(), tempName.c_str());
    } else {
        snprintf(tempPath, sizeof(tempPath), "/tmp/%s", tempName.c_str());
    }

    // mkdtemp 会修改模板并创建目录
    char* result = mkdtemp(tempPath);
    if (result == nullptr) {
        return "";
    }
    return std::string(result);
}

/// 检查文件是否存在
static bool fileExists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

/// 递归删除目录
static void removeDir(const std::string& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
}

// ───────── 占位符匹配与替换核心 ─────────

/// 占位符匹配信息
struct PlaceholderMatch {
    size_t startPos;       ///< {{ 在段落全文中的起始位置
    size_t endPos;         ///< }} 在段落全文中的结束位置（不含）
    std::string name;      ///< 占位符名称文本
    std::string replacement;  ///< 替换文本
};

/// 在段落全文中搜索所有 {{...}} 占位符
/// @param fullText 段落全文
/// @param replacements 占位符名称→替换文本 的映射
/// @return 匹配列表（含替换文本）
static std::vector<PlaceholderMatch> findPlaceholders(
    const std::string& fullText,
    const std::map<std::string, std::string>& replacements) {

    std::vector<PlaceholderMatch> matches;
    size_t searchPos = 0;

    while (true) {
        // 查找 {{
        size_t start = fullText.find("{{", searchPos);
        if (start == std::string::npos) break;

        // 查找对应的 }}
        size_t end = fullText.find("}}", start + 2);
        if (end == std::string::npos) break;

        // 提取占位符名称
        std::string name = fullText.substr(start + 2, end - start - 2);

        // 查找匹配的替换规则
        // 先检查精确匹配，再检查通配符 "*"
        std::string replacement;
        bool matched = false;

        auto it = replacements.find(name);
        if (it != replacements.end()) {
            replacement = it->second;
            matched = true;
        }

        if (!matched) {
            auto wildcard = replacements.find("*");
            if (wildcard != replacements.end()) {
                replacement = wildcard->second;
                matched = true;
            }
        }

        if (!matched) {
            searchPos = end + 2;
            continue;
        }

        PlaceholderMatch match;
        match.startPos = start;
        match.endPos = end + 2;  // 包含 }}
        match.name = name;
        match.replacement = replacement;
        matches.push_back(match);

        searchPos = end + 2;
    }

    return matches;
}

/// 对一个段落中的占位符进行替换
///
/// 核心算法：逐字符遍历每个 run 的文本，根据占位符匹配信息决定
/// 每个字符是保留、跳过还是在跳过前插入替换文本。
///
/// 对于匹配区间 [match.startPos, match.endPos) 内的字符：
///   - match.startPos 位置：插入替换文本
///   - 其它位置：跳过（属于 {{name}} 的一部分，已被替换文本取代）
/// 对于匹配区间外的字符：保留原样
///
/// @param paragraph <w:p> 段落节点
/// @param replacements 占位符名称→替换文本 的映射
/// @param records 替换记录（输出）
/// @param verbose 是否输出详细日志
/// @return 替换次数
static int replaceInParagraph(
    pugi::xml_node paragraph,
    const std::map<std::string, std::string>& replacements,
    std::vector<ReplaceRecord>& records,
    bool verbose) {

    // 收集段落中所有 run 的信息
    std::vector<RunInfo> runs;
    std::string fullText = collectRuns(paragraph, runs);

    if (fullText.find("{{") == std::string::npos) {
        return 0;  // 段落中没有占位符
    }

    // 搜索所有匹配的占位符（包含替换文本）
    auto matches = findPlaceholders(fullText, replacements);
    if (matches.empty()) {
        return 0;
    }

    if (verbose) {
        for (const auto& m : matches) {
            std::cout << "  替换: {{" << m.name << "}} -> "
                      << m.replacement << std::endl;
        }
    }

    // 对每个 run，根据匹配信息计算新文本
    // 逐字符处理：匹配区间内的字符被跳过，在区间起始处插入替换文本
    for (const auto& run : runs) {
        std::string newText;

        for (size_t j = 0; j < run.text.size(); j++) {
            size_t absPos = run.startPos + j;  // 该字符在全文中的绝对位置

            // 检查该字符是否在某个匹配区间内
            bool insideMatch = false;
            bool atMatchStart = false;
            const PlaceholderMatch* currentMatch = nullptr;

            for (const auto& match : matches) {
                if (absPos >= match.startPos && absPos < match.endPos) {
                    insideMatch = true;
                    if (absPos == match.startPos) {
                        atMatchStart = true;
                        currentMatch = &match;
                    }
                    break;
                }
            }

            // 在匹配区间起始处插入替换文本
            if (atMatchStart && currentMatch) {
                newText += currentMatch->replacement;
            }

            // 匹配区间内的字符跳过（已被替换文本取代）
            if (!insideMatch) {
                newText += run.text[j];
            }
        }

        // 如果文本有变化，更新 run
        if (newText != run.text) {
            setRunText(run.runNode, newText);
        }
    }

    // 记录替换结果
    int replaceCount = 0;
    for (const auto& match : matches) {
        ReplaceRecord record;
        record.placeholder = "{{" + match.name + "}}";
        record.replacement = match.replacement;
        record.count = 1;
        records.push_back(record);
        replaceCount++;
    }

    return replaceCount;
}

// ───────── 内部实现：核心替换流程 ─────────

static ReplaceResult replacePlaceholdersImpl(
    const std::string& inputPath,
    const std::map<std::string, std::string>& replacements,
    const std::string& outputPath,
    const ReplaceOptions& options) {

    ReplaceResult result;

    // 1. 参数校验
    if (replacements.empty()) {
        result.error = makeError(ErrorCode::InvalidPattern,
            "替换映射为空", "replacements map is empty");
        return result;
    }

    if (!fileExists(inputPath)) {
        result.error = makeError(ErrorCode::FileNotFound,
            "输入文件不存在", "path: " + inputPath);
        return result;
    }

    // 2. 创建临时目录
    std::string tempDir;
    if (!options.tempDir.empty()) {
        // 使用用户指定的临时目录
        tempDir = options.tempDir;
        std::error_code ec;
        fs::create_directories(tempDir, ec);
        if (ec) {
            result.error = makeError(ErrorCode::TempDirCreateFailed,
                "无法创建临时目录", "path: " + tempDir + ", error: " + ec.message());
            return result;
        }
    } else {
        tempDir = createTempDir("");
        if (tempDir.empty()) {
            result.error = makeError(ErrorCode::TempDirCreateFailed,
                "无法创建系统临时目录", "/tmp/docx_*");
            return result;
        }
    }

    if (options.verbose) {
        std::cout << "临时目录: " << tempDir << std::endl;
    }

    // 3. 解压 docx 到临时目录
    if (!unzipToDir(inputPath, tempDir)) {
        if (!options.keepTempDir) removeDir(tempDir);
        result.error = makeError(ErrorCode::UnzipFailed,
            "解压 docx 文件失败", "file: " + inputPath);
        return result;
    }

    if (options.verbose) {
        std::cout << "解压完成" << std::endl;
    }

    // 4. 加载并解析 word/document.xml
    std::string xmlPath = tempDir + "/word/document.xml";
    pugi::xml_document doc;
    pugi::xml_parse_result parseResult = doc.load_file(xmlPath.c_str());

    if (!parseResult) {
        if (!options.keepTempDir) removeDir(tempDir);
        result.error = makeError(ErrorCode::XmlParseFailed,
            "XML 解析失败",
            "file: " + xmlPath + ", error: " + parseResult.description());
        return result;
    }

    if (options.verbose) {
        std::cout << "XML 解析完成" << std::endl;
    }

    // 5. 遍历所有 <w:p> 段落节点，执行替换
    pugi::xml_node root = doc.document_element();  // <w:document>

    // 递归遍历所有 <w:p> 节点（包括表格单元格中的段落）
    int totalReplaced = 0;

    std::function<void(pugi::xml_node)> traverseNodes =
        [&](pugi::xml_node node) {
        for (pugi::xml_node child = node.first_child(); child;
             child = child.next_sibling()) {
            if (strcmp(child.name(), "w:p") == 0) {
                // 找到段落，执行替换
                totalReplaced += replaceInParagraph(
                    child, replacements, result.records, options.verbose);
            }
            // 递归处理子节点（如表格中的段落）
            traverseNodes(child);
        }
    };

    traverseNodes(root);

    result.totalReplaced = totalReplaced;

    if (options.verbose) {
        std::cout << "替换完成，总替换次数: " << totalReplaced << std::endl;
    }

    // 6. 保存修改后的 XML
    if (!doc.save_file(xmlPath.c_str())) {
        if (!options.keepTempDir) removeDir(tempDir);
        result.error = makeError(ErrorCode::XmlSaveFailed,
            "保存 XML 文件失败", "file: " + xmlPath);
        return result;
    }

    // 7. 重新压缩为 docx
    if (!zipDir(tempDir, outputPath)) {
        if (!options.keepTempDir) removeDir(tempDir);
        result.error = makeError(ErrorCode::ZipFailed,
            "压缩为 docx 文件失败", "output: " + outputPath);
        return result;
    }

    if (options.verbose) {
        std::cout << "输出文件: " << outputPath << std::endl;
    }

    // 8. 清理临时目录
    if (!options.keepTempDir) {
        removeDir(tempDir);
    }

    // 9. 检查是否有替换发生
    if (totalReplaced == 0) {
        result.error = makeError(ErrorCode::NoMatchFound,
            "未找到匹配的占位符",
            "pattern keys: " + std::to_string(replacements.size()) + " entries");
        return result;
    }

    return result;
}

// ───────── 公开接口实现 ─────────

// 【接口1】批量替换
ReplaceResult replacePlaceholders(
    const std::string& inputPath,
    const std::map<std::string, std::string>& replacements,
    const std::string& outputPath,
    const ReplaceOptions& options) {

    return replacePlaceholdersImpl(inputPath, replacements, outputPath, options);
}

// 【接口2】单模式替换
ReplaceResult replacePlaceholders(
    const std::string& inputPath,
    const std::string& pattern,
    const std::string& replacement,
    const std::string& outputPath,
    const ReplaceOptions& options) {

    // 解析 pattern，转换为 map 后委托给接口1
    std::map<std::string, std::string> replacements;

    // pattern 格式：{{*}} 或 {{具体名称}}
    // 提取 {{ }} 之间的内容作为 key
    if (pattern.size() >= 4 && pattern.substr(0, 2) == "{{"
        && pattern.substr(pattern.size() - 2) == "}}") {
        std::string key = pattern.substr(2, pattern.size() - 4);
        replacements[key] = replacement;
    } else if (pattern == "*") {
        replacements["*"] = replacement;
    } else {
        // 不带 {{}} 的，直接作为 key
        replacements[pattern] = replacement;
    }

    return replacePlaceholdersImpl(inputPath, replacements, outputPath, options);
}

// ───────── 接口3：富文本替换实现 ─────────

/// 检查段落是否只包含指定的占位符（独占一行）
/// @param paragraph <w:p> 段落节点
/// @param placeholderName 占位符名称
/// @return 若段落全文只有 {{placeholderName}} 则返回 true
static bool isParagraphOnlyPlaceholder(pugi::xml_node paragraph,
                                        const std::string& placeholderName) {
    std::vector<RunInfo> runs;
    std::string fullText = collectRuns(paragraph, runs);

    // 去除首尾空白后检查
    std::string trimmed = fullText;
    // 去除前导空白
    size_t start = trimmed.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return false;
    // 去除尾部空白
    size_t end = trimmed.find_last_not_of(" \t\r\n");
    trimmed = trimmed.substr(start, end - start + 1);

    std::string expected = "{{" + placeholderName + "}}";
    return trimmed == expected;
}

ReplaceResult replacePlaceholdersRich(
    const std::string& inputPath,
    const std::map<std::string, RichReplacement>& replacements,
    const std::string& outputPath,
    const ReplaceOptions& options) {

    ReplaceResult result;

    // 参数校验
    if (replacements.empty()) {
        result.error = makeError(ErrorCode::InvalidPattern,
            "替换映射为空", "replacements map is empty");
        return result;
    }

    if (!fileExists(inputPath)) {
        result.error = makeError(ErrorCode::FileNotFound,
            "输入文件不存在", "path: " + inputPath);
        return result;
    }

    // 将纯文本替换项分离出来，走原有逻辑
    std::map<std::string, std::string> plainReplacements;
    std::map<std::string, RichReplacement> richReplacements;

    for (const auto& [key, rich] : replacements) {
        if (rich.type == ContentType::Plain) {
            plainReplacements[key] = rich.content;
        } else {
            richReplacements[key] = rich;
        }
    }

    // 创建临时目录
    std::string tempDir;
    if (!options.tempDir.empty()) {
        tempDir = options.tempDir;
        std::error_code ec;
        fs::create_directories(tempDir, ec);
        if (ec) {
            result.error = makeError(ErrorCode::TempDirCreateFailed,
                "无法创建临时目录", "path: " + tempDir + ", error: " + ec.message());
            return result;
        }
    } else {
        tempDir = createTempDir("");
        if (tempDir.empty()) {
            result.error = makeError(ErrorCode::TempDirCreateFailed,
                "无法创建系统临时目录", "/tmp/docx_*");
            return result;
        }
    }

    if (options.verbose) {
        std::cout << "临时目录: " << tempDir << std::endl;
    }

    // 解压
    if (!unzipToDir(inputPath, tempDir)) {
        if (!options.keepTempDir) removeDir(tempDir);
        result.error = makeError(ErrorCode::UnzipFailed,
            "解压 docx 文件失败", "file: " + inputPath);
        return result;
    }

    // 加载 XML
    std::string xmlPath = tempDir + "/word/document.xml";
    pugi::xml_document doc;
    pugi::xml_parse_result parseResult = doc.load_file(xmlPath.c_str());
    if (!parseResult) {
        if (!options.keepTempDir) removeDir(tempDir);
        result.error = makeError(ErrorCode::XmlParseFailed,
            "XML 解析失败",
            "file: " + xmlPath + ", error: " + parseResult.description());
        return result;
    }

    pugi::xml_node root = doc.document_element();
    int totalReplaced = 0;

    // ── 阶段1：先处理富文本替换（HTML/Markdown）──
    // 富文本替换需要替换整个段落，必须在纯文本替换之前进行，
    // 因为纯文本替换会修改 <w:t> 内容但不删除段落
    if (!richReplacements.empty()) {
        std::function<void(pugi::xml_node)> richTraverse =
            [&](pugi::xml_node node) {
            // 先收集所有 <w:p> 子节点（避免遍历时修改导致迭代器失效）
            std::vector<pugi::xml_node> paragraphs;
            for (pugi::xml_node child = node.first_child(); child;
                 child = child.next_sibling()) {
                if (strcmp(child.name(), "w:p") == 0) {
                    paragraphs.push_back(child);
                }
            }

            // 对每个段落检查是否匹配富文本替换
            for (auto& para : paragraphs) {
                for (const auto& [key, rich] : richReplacements) {
                    bool match = false;
                    if (key == "*") {
                        // 通配符：检查段落是否只包含 {{任意}}
                        std::vector<RunInfo> runs;
                        std::string fullText = collectRuns(para, runs);
                        // 去除空白
                        size_t s = fullText.find_first_not_of(" \t\r\n");
                        if (s == std::string::npos) continue;
                        size_t e = fullText.find_last_not_of(" \t\r\n");
                        std::string trimmed = fullText.substr(s, e - s + 1);
                        // 检查是否是 {{...}} 模式
                        if (trimmed.size() >= 4 &&
                            trimmed.substr(0, 2) == "{{" &&
                            trimmed.substr(trimmed.size() - 2) == "}}") {
                            match = true;
                        }
                    } else {
                        match = isParagraphOnlyPlaceholder(para, key);
                    }

                    if (match) {
                        if (options.verbose) {
                            std::cout << "  富文本替换: {{" << key << "}} ("
                                      << (rich.type == ContentType::HTML ? "HTML" : "MD")
                                      << ")" << std::endl;
                        }

                        // 解析内容
                        std::vector<RichParagraph> richParas;
                        if (rich.type == ContentType::HTML) {
                            richParas = parseHtml(rich.content);
                        } else {
                            richParas = parseMarkdown(rich.content);
                        }

                        // 渲染并替换段落
                        pugi::xml_node parent = para.parent();
                        renderParagraphsToXml(parent, richParas, para);

                        // 记录替换
                        ReplaceRecord record;
                        record.placeholder = "{{" + key + "}}";
                        record.replacement = "[RichContent:" +
                            std::string(rich.type == ContentType::HTML ? "HTML" : "MD") +
                            ":" + std::to_string(richParas.size()) + " paragraphs]";
                        record.count = 1;
                        result.records.push_back(record);
                        totalReplaced++;
                        break;  // 一个段落只匹配一个替换项
                    }
                }
            }

            // 递归处理子节点
            for (pugi::xml_node child = node.first_child(); child;
                 child = child.next_sibling()) {
                richTraverse(child);
            }
        };

        richTraverse(root);
    }

    // ── 阶段2：处理纯文本替换 ──
    if (!plainReplacements.empty()) {
        std::function<void(pugi::xml_node)> plainTraverse =
            [&](pugi::xml_node node) {
            for (pugi::xml_node child = node.first_child(); child;
                 child = child.next_sibling()) {
                if (strcmp(child.name(), "w:p") == 0) {
                    totalReplaced += replaceInParagraph(
                        child, plainReplacements, result.records, options.verbose);
                }
                plainTraverse(child);
            }
        };

        plainTraverse(root);
    }

    result.totalReplaced = totalReplaced;

    if (options.verbose) {
        std::cout << "替换完成，总替换次数: " << totalReplaced << std::endl;
    }

    // 保存 XML
    if (!doc.save_file(xmlPath.c_str())) {
        if (!options.keepTempDir) removeDir(tempDir);
        result.error = makeError(ErrorCode::XmlSaveFailed,
            "保存 XML 文件失败", "file: " + xmlPath);
        return result;
    }

    // 重新压缩
    if (!zipDir(tempDir, outputPath)) {
        if (!options.keepTempDir) removeDir(tempDir);
        result.error = makeError(ErrorCode::ZipFailed,
            "压缩为 docx 文件失败", "output: " + outputPath);
        return result;
    }

    if (options.verbose) {
        std::cout << "输出文件: " << outputPath << std::endl;
    }

    // 清理
    if (!options.keepTempDir) {
        removeDir(tempDir);
    }

    if (totalReplaced == 0) {
        result.error = makeError(ErrorCode::NoMatchFound,
            "未找到匹配的占位符",
            "replacements: " + std::to_string(replacements.size()) + " entries");
        return result;
    }

    return result;
}

} // namespace docx_temp_helper
