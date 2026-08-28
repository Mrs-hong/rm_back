//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

/**
 * @file rich_content.cpp
 * @brief 富文本内容解析与渲染实现
 *
 * 包含三部分：
 *   1. Markdown 解析器：逐行解析 MD 语法为 RichParagraph 列表
 *   2. HTML 解析器：用 pugixml 解析 HTML DOM 树为 RichParagraph 列表
 *   3. OOXML 渲染器：将 RichParagraph 列表渲染为 <w:p> XML 元素
 *
 * 格式标准参考 GB/T 9704-2012：
 *   字号换算：OOXML 用半磅(pt*2)，缩进用 twips(pt*20)
 *   h1=22pt(44) h2-h4=16pt(32) 正文=16pt(32)
 *   首行缩进=32pt(640twips) 行距=1.0(240)
 */

#include "common/utils/docx/rich_content.h"

#include <pugixml.hpp>
#include <json/reader.h>
#include <json/value.h>
#include "qifeng_framework/common/logger.h"

#include <cstring>
#include <sstream>
#include <algorithm>
#include <regex>
#include <memory>

namespace qifeng_ca::docx {

// ═══════════════════════════════════════════════════════════════════════════════
// 常量定义（GB/T 9704-2012 公文格式标准）
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

// 字体名称
const char* FONT_TITLE   = "方正小标宋简体";  // h1 标题字体
const char* FONT_HEADING = "黑体";            // h2 标题字体
const char* FONT_SUBHEAD = "楷体";            // h3 标题字体
const char* FONT_BODY    = "仿宋";            // h4/正文/列表字体
const char* FONT_WESTERN = "Times New Roman"; // 西文字体

// 字号（半磅，OOXML 中 w:sz 的值）
const int SIZE_H1    = 44;  // 22pt
const int SIZE_BODY  = 32;  // 16pt

// 缩进（twips，1pt=20twips）
const int INDENT_FIRST_LINE = 640;   // 32pt 首行缩进（2个三号字）
const int INDENT_LIST_LEFT  = 640;   // 32pt 列表左缩进
const int INDENT_LIST_HANG  = 320;   // 16pt 列表悬挂缩进

// 行距（240 = 1.0 倍行距）
const int LINE_SPACING_10 = 240;

// 段后间距
const int SPACE_AFTER_HEADING = 160;  // 8pt

/// 获取标题级别对应的字体
const char* GetHeadingFont(int level) {
    switch (level) {
        case 1:  return FONT_TITLE;
        case 2:  return FONT_HEADING;
        case 3:  return FONT_SUBHEAD;
        default: return FONT_BODY;
    }
}

/// 获取标题级别对应的字号（半磅）
int GetHeadingSize(int level) {
    return (level == 1) ? SIZE_H1 : SIZE_BODY;
}

/// 获取标题级别对应的对齐方式
const char* GetHeadingAlignment(int level) {
    return (level == 1) ? "center" : "left";
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Markdown 解析器
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

/// 解析行内格式（加粗、斜体）为 RichRun 列表
/// 支持: **bold**, *italic*, ***bold+italic***
std::vector<RichRun> ParseInlineMarkdown(const std::string& text) {
    std::vector<RichRun> runs;

    // 按顺序匹配：先 ***...***(粗斜体), 再 **...**(粗体), 再 *...*(斜体)
    size_t lastEnd = 0;

    size_t pos = 0;
    while (pos < text.size()) {
        // 尝试 ***bold+italic***
        if (pos + 3 <= text.size() && text[pos] == '*' && text[pos+1] == '*' && text[pos+2] == '*') {
            size_t end = text.find("***", pos + 3);
            if (end != std::string::npos) {
                // 添加前面的普通文本
                if (pos > lastEnd) {
                    RichRun r;
                    r.text = text.substr(lastEnd, pos - lastEnd);
                    runs.push_back(r);
                }
                RichRun r;
                r.text = text.substr(pos + 3, end - pos - 3);
                r.bold = true;
                r.italic = true;
                runs.push_back(r);
                lastEnd = end + 3;
                pos = end + 3;
                continue;
            }
        }

        // 尝试 **bold**
        if (pos + 2 <= text.size() && text[pos] == '*' && text[pos+1] == '*') {
            size_t end = text.find("**", pos + 2);
            if (end != std::string::npos) {
                if (pos > lastEnd) {
                    RichRun r;
                    r.text = text.substr(lastEnd, pos - lastEnd);
                    runs.push_back(r);
                }
                RichRun r;
                r.text = text.substr(pos + 2, end - pos - 2);
                r.bold = true;
                runs.push_back(r);
                lastEnd = end + 2;
                pos = end + 2;
                continue;
            }
        }

        // 尝试 *italic*
        if (pos + 1 <= text.size() && text[pos] == '*') {
            size_t end = text.find('*', pos + 1);
            if (end != std::string::npos && end > pos + 1) {
                if (pos > lastEnd) {
                    RichRun r;
                    r.text = text.substr(lastEnd, pos - lastEnd);
                    runs.push_back(r);
                }
                RichRun r;
                r.text = text.substr(pos + 1, end - pos - 1);
                r.italic = true;
                runs.push_back(r);
                lastEnd = end + 1;
                pos = end + 1;
                continue;
            }
        }

        pos++;
    }

    // 添加剩余的普通文本
    if (lastEnd < text.size()) {
        RichRun r;
        r.text = text.substr(lastEnd);
        runs.push_back(r);
    }

    // 如果没有匹配到任何格式，返回整个文本作为一个 run
    if (runs.empty()) {
        RichRun r;
        r.text = text;
        runs.push_back(r);
    }

    return runs;
}

/// 去除行首尾空白
std::string Trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

/// 检查字符串是否以指定前缀开头
bool StartsWith(const std::string& s, const char* prefix) {
    return s.compare(0, strlen(prefix), prefix) == 0;
}

} // anonymous namespace

std::vector<RichParagraph> ParseMarkdown(const std::string& md) {
    std::vector<RichParagraph> paragraphs;

    std::istringstream stream(md);
    std::string line;

    int orderedListIndex = 0;  // 有序列表计数器

    while (std::getline(stream, line)) {
        std::string trimmed = Trim(line);

        // 空行：跳过（段落分隔符）
        if (trimmed.empty()) {
            orderedListIndex = 0;  // 重置有序列表计数
            continue;
        }

        RichParagraph para;

        // 检查标题: # ~ ####
        if (StartsWith(trimmed, "#### ")) {
            para.headingLevel = 4;
            std::string text = trimmed.substr(5);
            para.runs = ParseInlineMarkdown(text);
        } else if (StartsWith(trimmed, "### ")) {
            para.headingLevel = 3;
            std::string text = trimmed.substr(4);
            para.runs = ParseInlineMarkdown(text);
        } else if (StartsWith(trimmed, "## ")) {
            para.headingLevel = 2;
            std::string text = trimmed.substr(3);
            para.runs = ParseInlineMarkdown(text);
        } else if (StartsWith(trimmed, "# ")) {
            para.headingLevel = 1;
            std::string text = trimmed.substr(2);
            para.runs = ParseInlineMarkdown(text);
        }
        // 检查无序列表: - 或 *
        else if (StartsWith(trimmed, "- ") || StartsWith(trimmed, "* ")) {
            para.isListItem = true;
            para.isOrdered = false;
            std::string text = trimmed.substr(2);
            para.runs = ParseInlineMarkdown(text);
        }
        // 检查有序列表: 1. 2. 3. 等
        else {
            // 匹配 "数字. " 开头
            size_t dotPos = trimmed.find(". ");
            if (dotPos != std::string::npos && dotPos > 0) {
                bool allDigits = true;
                for (size_t i = 0; i < dotPos; i++) {
                    if (!isdigit(trimmed[i])) {
                        allDigits = false;
                        break;
                    }
                }
                if (allDigits) {
                    para.isListItem = true;
                    para.isOrdered = true;
                    orderedListIndex++;
                    para.listIndex = orderedListIndex;
                    std::string text = trimmed.substr(dotPos + 2);
                    para.runs = ParseInlineMarkdown(text);
                } else {
                    // 普通段落
                    orderedListIndex = 0;
                    para.runs = ParseInlineMarkdown(trimmed);
                }
            } else {
                // 普通段落
                orderedListIndex = 0;
                para.runs = ParseInlineMarkdown(trimmed);
            }
        }

        // 设置默认格式
        if (para.headingLevel > 0) {
            // 标题格式
            para.alignment = GetHeadingAlignment(para.headingLevel);
            para.spaceAfter = SPACE_AFTER_HEADING;
            para.lineSpacing = 1.0;
        } else if (para.isListItem) {
            // 列表项格式
            para.alignment = "left";
            para.leftIndent = INDENT_LIST_LEFT;
            para.hangingIndent = INDENT_LIST_HANG;
            para.lineSpacing = 1.0;

            // 添加列表前缀
            std::string prefix;
            if (para.isOrdered) {
                prefix = std::to_string(para.listIndex) + ". ";
            } else {
                prefix = "\xE2\x80\xA2 ";  // UTF-8 "• "
            }
            if (!para.runs.empty()) {
                para.runs[0].text = prefix + para.runs[0].text;
            }
        } else {
            // 正文格式
            para.alignment = "justify";
            para.firstLineIndent = INDENT_FIRST_LINE;
            para.lineSpacing = 1.0;
        }

        paragraphs.push_back(para);
    }

    return paragraphs;
}

// ═══════════════════════════════════════════════════════════════════════════════
// HTML 解析器
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

/// 递归解析 HTML 元素的子节点，提取 RichRun 列表
/// @param node HTML 元素节点
/// @param inheritedBold 父级继承的加粗状态
/// @param inheritedItalic 父级继承的斜体状态
/// @param inheritedUnderline 父级继承的下划线状态
std::vector<RichRun> ParseHtmlInline(pugi::xml_node node,
                                      bool inheritedBold = false,
                                      bool inheritedItalic = false,
                                      bool inheritedUnderline = false) {
    std::vector<RichRun> runs;

    for (pugi::xml_node child = node.first_child(); child;
         child = child.next_sibling()) {
        if (child.type() == pugi::node_pcdata) {
            // 文本节点
            std::string text = child.value();
            if (!text.empty()) {
                RichRun r;
                r.text = text;
                r.bold = inheritedBold;
                r.italic = inheritedItalic;
                r.underline = inheritedUnderline;
                runs.push_back(r);
            }
        } else if (child.type() == pugi::node_element) {
            std::string tagName = child.name();

            if (tagName == "b" || tagName == "strong") {
                // 加粗
                auto childRuns = ParseHtmlInline(child, true, inheritedItalic, inheritedUnderline);
                runs.insert(runs.end(), childRuns.begin(), childRuns.end());
            } else if (tagName == "i" || tagName == "em") {
                // 斜体
                auto childRuns = ParseHtmlInline(child, inheritedBold, true, inheritedUnderline);
                runs.insert(runs.end(), childRuns.begin(), childRuns.end());
            } else if (tagName == "u") {
                // 下划线
                auto childRuns = ParseHtmlInline(child, inheritedBold, inheritedItalic, true);
                runs.insert(runs.end(), childRuns.begin(), childRuns.end());
            } else if (tagName == "br") {
                // 换行：插入空 run 作为分隔（实际渲染时可以忽略）
                RichRun r;
                r.text = "\n";
                runs.push_back(r);
            } else if (tagName == "span") {
                // span：直接递归（忽略 style 属性中的格式）
                auto childRuns = ParseHtmlInline(child, inheritedBold, inheritedItalic, inheritedUnderline);
                runs.insert(runs.end(), childRuns.begin(), childRuns.end());
            } else {
                // 其他标签：递归提取文本
                auto childRuns = ParseHtmlInline(child, inheritedBold, inheritedItalic, inheritedUnderline);
                runs.insert(runs.end(), childRuns.begin(), childRuns.end());
            }
        }
    }

    return runs;
}

/// 从 HTML 元素创建段落
RichParagraph CreateHtmlParagraph(pugi::xml_node element, int headingLevel = 0) {
    RichParagraph para;
    para.headingLevel = headingLevel;
    para.runs = ParseHtmlInline(element);

    if (headingLevel > 0) {
        para.alignment = GetHeadingAlignment(headingLevel);
        para.spaceAfter = SPACE_AFTER_HEADING;
        para.lineSpacing = 1.0;
    } else {
        para.alignment = "justify";
        para.firstLineIndent = INDENT_FIRST_LINE;
        para.lineSpacing = 1.0;
    }

    return para;
}

} // anonymous namespace

std::vector<RichParagraph> ParseHtml(const std::string& html) {
    std::vector<RichParagraph> paragraphs;

    // 用 pugixml 解析 HTML
    // 包装在根元素中以确保 XML 合法
    std::string wrapped = "<root>" + html + "</root>";

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_string(wrapped.c_str(),
        pugi::parse_default | pugi::parse_pi | pugi::parse_comments);

    if (!result) {
        // 解析失败，将原始文本作为一个普通段落返回
        RichParagraph para;
        RichRun run;
        run.text = html;
        para.runs.push_back(run);
        para.alignment = "justify";
        para.firstLineIndent = INDENT_FIRST_LINE;
        paragraphs.push_back(para);
        return paragraphs;
    }

    pugi::xml_node root = doc.child("root");

    // 遍历 HTML 元素
    for (pugi::xml_node element = root.first_child(); element;
         element = element.next_sibling()) {
        if (element.type() != pugi::node_element) continue;

        std::string tagName = element.name();

        if (tagName == "h1") {
            paragraphs.push_back(CreateHtmlParagraph(element, 1));
        } else if (tagName == "h2") {
            paragraphs.push_back(CreateHtmlParagraph(element, 2));
        } else if (tagName == "h3") {
            paragraphs.push_back(CreateHtmlParagraph(element, 3));
        } else if (tagName == "h4" || tagName == "h5" || tagName == "h6") {
            paragraphs.push_back(CreateHtmlParagraph(element, 4));
        } else if (tagName == "p") {
            paragraphs.push_back(CreateHtmlParagraph(element, 0));
        } else if (tagName == "ul" || tagName == "ol") {
            // 列表
            bool isOrdered = (tagName == "ol");
            int index = 0;
            for (pugi::xml_node li = element.child("li"); li;
                 li = li.next_sibling("li")) {
                index++;
                RichParagraph para;
                para.isListItem = true;
                para.isOrdered = isOrdered;
                para.listIndex = index;
                para.runs = ParseHtmlInline(li);

                // 添加列表前缀
                std::string prefix;
                if (isOrdered) {
                    prefix = std::to_string(index) + ". ";
                } else {
                    prefix = "\xE2\x80\xA2 ";  // UTF-8 "• "
                }
                if (!para.runs.empty()) {
                    para.runs[0].text = prefix + para.runs[0].text;
                }

                para.alignment = "left";
                para.leftIndent = INDENT_LIST_LEFT;
                para.hangingIndent = INDENT_LIST_HANG;
                para.lineSpacing = 1.0;

                paragraphs.push_back(para);
            }
        } else if (tagName == "br") {
            // 顶层 <br>，跳过
        } else {
            // 其他标签：尝试作为段落处理
            auto runs = ParseHtmlInline(element);
            if (!runs.empty()) {
                RichParagraph para;
                para.runs = runs;
                para.alignment = "justify";
                para.firstLineIndent = INDENT_FIRST_LINE;
                para.lineSpacing = 1.0;
                paragraphs.push_back(para);
            }
        }
    }

    // 如果没有解析到任何段落，返回原始文本
    if (paragraphs.empty()) {
        RichParagraph para;
        RichRun run;
        run.text = html;
        para.runs.push_back(run);
        para.alignment = "justify";
        para.firstLineIndent = INDENT_FIRST_LINE;
        paragraphs.push_back(para);
    }

    return paragraphs;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Editor.js JSON 解析器
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

/// 解析 Editor.js block 的内联文本为 RichRun 列表
/// @param text 可能含内联 HTML(<b>/<i>/<u>)的文本
/// @note 复用 HTML 内联解析器 ParseHtmlInline, 将文本包装到 <root> 中解析
std::vector<RichRun> ParseEditorJsInline(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    // 不含 HTML 标签则直接作为普通文本
    if (text.find('<') == std::string::npos) {
        RichRun r;
        r.text = text;
        return {r};
    }
    // 包装为 <root>...</root> 后用 pugixml 解析, 复用 HTML 内联解析逻辑
    std::string wrapped = "<root>" + text + "</root>";
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_string(wrapped.c_str(),
        pugi::parse_default | pugi::parse_pi | pugi::parse_comments);
    if (!result) {
        // 解析失败, 降级为纯文本
        RichRun r;
        r.text = text;
        return {r};
    }
    pugi::xml_node root = doc.child("root");
    return ParseHtmlInline(root);
}

/// 递归处理 Editor.js 列表 items, 生成 RichParagraph 列表
/// @param items Json::Value 数组, 每项含 content 字符串和可选的嵌套 items
/// @param isOrdered 是否为有序列表
/// @param startIndex 有序列表起始序号(用于嵌套时重置)
/// @param level 嵌套层级(0=顶层), 影响左缩进
/// @param out 输出段落列表
void AppendEditorJsListItems(const Json::Value& items, bool isOrdered,
                              int startIndex, int level,
                              std::vector<RichParagraph>& out) {
    int index = startIndex;
    int leftIndent = INDENT_LIST_LEFT + level * INDENT_LIST_LEFT;  // 每层缩进递增
    for (Json::ArrayIndex i = 0; i < items.size(); ++i) {
        const Json::Value& item = items[i];
        if (!item.isObject()) continue;

        std::string content = item["content"].asString();
        if (content.empty()) continue;

        RichParagraph para;
        para.isListItem = true;
        para.isOrdered = isOrdered;
        if (isOrdered) {
            para.listIndex = index++;
        }
        para.runs = ParseEditorJsInline(content);

        // 添加列表前缀
        std::string prefix;
        if (isOrdered) {
            prefix = std::to_string(para.listIndex) + ". ";
        } else {
            prefix = "\xE2\x80\xA2 ";  // UTF-8 "• "
        }
        if (!para.runs.empty()) {
            para.runs[0].text = prefix + para.runs[0].text;
        }

        para.alignment = "left";
        para.leftIndent = leftIndent;
        para.hangingIndent = INDENT_LIST_HANG;
        para.lineSpacing = 1.0;
        out.push_back(para);

        // 递归处理嵌套 items
        if (item.isMember("items") && item["items"].isArray() && !item["items"].empty()) {
            AppendEditorJsListItems(item["items"], isOrdered, 1, level + 1, out);
        }
    }
}

/// 将 Editor.js 表格 block 解析为 RichTable 结构(渲染为真正的 OOXML <w:tbl> 表格)
/// @param data 表格 block 的 data 字段: {withHeadings, content: 二维字符串数组}
/// @param out 输出段落列表(追加一个 table 字段非空的 RichParagraph 块)
/// @note 列宽由渲染层均分; 不规整行(行列数不一致)补空单元格保证网格对齐
/// @note 单元格内容可能含内联 HTML(<b>/<i>/<u>), 复用 ParseEditorJsInline 解析
void AppendEditorJsTable(const Json::Value& data, std::vector<RichParagraph>& out) {
    const Json::Value& content = data["content"];
    if (!content.isArray() || content.empty()) {
        SLOG_WARN << "AppendEditorJsTable: content is not array or empty, skip";
        return;
    }
    bool withHeadings = data["withHeadings"].asBool();
    SLOG_INFO << "AppendEditorJsTable: enter, rows=" << content.size()
              << ", withHeadings=" << (withHeadings ? "true" : "false");

    auto table = std::make_shared<RichTable>();
    size_t maxCols = 0;
    for (Json::ArrayIndex row = 0; row < content.size(); ++row) {
        const Json::Value& cells = content[row];
        if (!cells.isArray() || cells.empty()) {
            SLOG_WARN << "AppendEditorJsTable: row " << row << " is not array or empty, skip";
            continue;
        }

        std::vector<RichTableCell> rowCells;
        rowCells.reserve(cells.size());
        for (Json::ArrayIndex col = 0; col < cells.size(); ++col) {
            RichTableCell cell;
            std::string cellText = cells[col].asString();
            if (!cellText.empty()) {
                // 单元格内容可能含内联 HTML, 复用内联解析为富文本 runs
                std::vector<RichRun> cellRuns = ParseEditorJsInline(cellText);
                if (cellRuns.empty()) {
                    RichRun r;
                    r.text = cellText;
                    cellRuns.push_back(r);
                }
                cell.runs = std::move(cellRuns);
            }
            rowCells.push_back(std::move(cell));
        }
        maxCols = std::max(maxCols, rowCells.size());
        table->rows.push_back(std::move(rowCells));
    }

    if (table->rows.empty() || maxCols == 0) {
        SLOG_WARN << "AppendEditorJsTable: no valid rows, skip";
        return;
    }

    // 不规整表格(各行列数不一致): 短行补空单元格, 保证 tblGrid 列对齐
    for (auto& row : table->rows) {
        row.resize(maxCols);
    }
    table->hasHeadingRow = withHeadings;

    // 追加一个表格块(渲染层检测到 table 非空时输出 <w:tbl>)
    RichParagraph para;
    para.table = std::move(table);
    out.push_back(std::move(para));

    SLOG_INFO << "AppendEditorJsTable: table built, rows=" << (out.empty() ? 0 : out.back().table->rows.size())
              << ", cols=" << maxCols << ", hasHeadingRow=" << (withHeadings ? "true" : "false");
}

} // anonymous namespace

std::vector<RichParagraph> ParseEditorJs(const std::string& json) {
    std::vector<RichParagraph> paragraphs;
    if (json.empty()) {
        SLOG_WARN << "ParseEditorJs: json is empty";
        return paragraphs;
    }
    SLOG_INFO << "ParseEditorJs: enter, json size=" << json.size();

    // 解析 JSON
    Json::Value root;
    Json::CharReaderBuilder readerBuilder;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(readerBuilder.newCharReader());
    if (!reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs)) {
        SLOG_ERROR << "ParseEditorJs: json parse failed, errs=" << errs;
        return paragraphs;  // 解析失败返回空, 由调用方降级
    }

    const Json::Value& blocks = root["blocks"];
    if (!blocks.isArray()) {
        SLOG_WARN << "ParseEditorJs: blocks is not array";
        return paragraphs;
    }
    SLOG_INFO << "ParseEditorJs: blocks count=" << blocks.size();

    for (Json::ArrayIndex i = 0; i < blocks.size(); ++i) {
        const Json::Value& block = blocks[i];
        if (!block.isObject()) continue;

        std::string type = block["type"].asString();
        const Json::Value& data = block["data"];
        SLOG_INFO << "ParseEditorJs: block[" << i << "] type=[" << type << "]";

        if (type == "header") {
            // 标题: level 1-4, 超出范围降级
            int level = data["level"].asInt();
            if (level < 1) level = 1;
            if (level > 4) level = 4;
            std::string text = data["text"].asString();
            if (text.empty()) continue;

            RichParagraph para;
            para.headingLevel = level;
            para.runs = ParseEditorJsInline(text);
            para.alignment = GetHeadingAlignment(level);
            para.spaceAfter = SPACE_AFTER_HEADING;
            para.lineSpacing = 1.0;
            paragraphs.push_back(para);
        } else if (type == "paragraph") {
            // 段落
            std::string text = data["text"].asString();
            if (text.empty()) continue;

            RichParagraph para;
            para.runs = ParseEditorJsInline(text);
            para.alignment = "justify";
            para.firstLineIndent = INDENT_FIRST_LINE;
            para.lineSpacing = 1.0;
            paragraphs.push_back(para);
        } else if (type == "list") {
            // 列表: 有序/无序, 支持嵌套
            if (!data.isMember("items") || !data["items"].isArray()) continue;
            std::string style = data["style"].asString();
            bool isOrdered = (style == "ordered");
            AppendEditorJsListItems(data["items"], isOrdered, 1, 0, paragraphs);
        } else if (type == "table") {
            // 表格: 降级渲染为段落(每行一个段落, 单元格间制表符分隔, 表头行加粗)
            SLOG_INFO << "ParseEditorJs: dispatch to AppendEditorJsTable, block[" << i << "]";
            AppendEditorJsTable(data, paragraphs);
        } else {
            SLOG_WARN << "ParseEditorJs: unsupported block type=[" << type << "], skipped";
        }
        // 其他类型(quote/code/delimiter/image 等)暂不支持, 跳过
    }

    SLOG_INFO << "ParseEditorJs: done, total paragraphs=" << paragraphs.size();
    return paragraphs;
}

// ═══════════════════════════════════════════════════════════════════════════════
// OOXML 渲染器
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

/// 创建 <w:rPr>（run 属性）节点
/// @param parent <w:r> 节点
/// @param font 中文字体名
/// @param size 字号（半磅）
/// @param bold 加粗
/// @param italic 斜体
/// @param underline 下划线
void CreateRunProperties(pugi::xml_node runNode,
                          const char* font, int size,
                          bool bold, bool italic, bool underline) {
    pugi::xml_node rPr = runNode.append_child("w:rPr");

    // 字体设置：w:eastAsia 为中文字体，w:ascii/w:hAnsi 为西文字体
    pugi::xml_node rFonts = rPr.append_child("w:rFonts");
    rFonts.append_attribute("w:ascii") = FONT_WESTERN;
    rFonts.append_attribute("w:hAnsi") = FONT_WESTERN;
    rFonts.append_attribute("w:eastAsia") = font;

    // 字号（半磅）
    rPr.append_child("w:sz").append_attribute("w:val") = size;
    rPr.append_child("w:szCs").append_attribute("w:val") = size;

    // 加粗
    if (bold) {
        rPr.append_child("w:b");
        rPr.append_child("w:bCs");
    }

    // 斜体
    if (italic) {
        rPr.append_child("w:i");
        rPr.append_child("w:iCs");
    }

    // 下划线
    if (underline) {
        pugi::xml_node u = rPr.append_child("w:u");
        u.append_attribute("w:val") = "single";
    }
}

/// 创建 <w:pPr>（段落属性）节点
/// @param pPr <w:pPr> 节点
/// @param para 段落信息
void CreateParagraphProperties(pugi::xml_node pPr, const RichParagraph& para) {
    // 行距：line=240 表示 1.0 倍行距
    pugi::xml_node spacing = pPr.append_child("w:spacing");
    int lineVal = static_cast<int>(LINE_SPACING_10 * para.lineSpacing);
    spacing.append_attribute("w:line") = lineVal;
    spacing.append_attribute("w:lineRule") = "auto";

    // 段后间距
    if (para.spaceAfter > 0) {
        spacing.append_attribute("w:after") = para.spaceAfter;
    }

    // 缩进
    if (para.firstLineIndent > 0 || para.leftIndent > 0 || para.hangingIndent > 0) {
        pugi::xml_node ind = pPr.append_child("w:ind");
        if (para.firstLineIndent > 0) {
            ind.append_attribute("w:firstLine") = para.firstLineIndent;
        }
        if (para.leftIndent > 0) {
            ind.append_attribute("w:left") = para.leftIndent;
        }
        if (para.hangingIndent > 0) {
            ind.append_attribute("w:hanging") = para.hangingIndent;
        }
    }

    // 对齐方式
    if (!para.alignment.empty()) {
        pugi::xml_node jc = pPr.append_child("w:jc");
        if (para.alignment == "center") {
            jc.append_attribute("w:val") = "center";
        } else if (para.alignment == "right") {
            jc.append_attribute("w:val") = "right";
        } else if (para.alignment == "justify") {
            jc.append_attribute("w:val") = "both";
        } else {
            jc.append_attribute("w:val") = "left";
        }
    }
}

/// 将一个 RichParagraph 渲染为 <w:p> 元素
/// @param parent 父节点
/// @param para 段落信息
/// @return 创建的 <w:p> 节点
pugi::xml_node CreateParagraphNode(pugi::xml_node parent, const RichParagraph& para) {
    pugi::xml_node pNode = parent.append_child("w:p");

    // 段落属性
    pugi::xml_node pPr = pNode.append_child("w:pPr");
    CreateParagraphProperties(pPr, para);

    // 确定字体和字号
    const char* font = FONT_BODY;
    int size = SIZE_BODY;
    if (para.headingLevel > 0) {
        font = GetHeadingFont(para.headingLevel);
        size = GetHeadingSize(para.headingLevel);
    }

    // 添加 runs
    for (const auto& run : para.runs) {
        if (run.text.empty()) continue;

        // 处理换行符（将 \n 拆分为多个 run，中间插入 <w:br/>）
        // 简化处理：直接替换为空格或跳过
        std::string text = run.text;
        // 跳过纯换行 run
        if (text == "\n") continue;

        pugi::xml_node rNode = pNode.append_child("w:r");
        CreateRunProperties(rNode, font, size, run.bold, run.italic, run.underline);

        pugi::xml_node tNode = rNode.append_child("w:t");
        tNode.append_attribute("xml:space") = "preserve";
        tNode.text().set(text.c_str());
    }

    // 如果没有 runs，添加一个空 run 以保证段落不为空
    if (pNode.child("w:r") == nullptr) {
        pugi::xml_node rNode = pNode.append_child("w:r");
        CreateRunProperties(rNode, font, size, false, false, false);
        pugi::xml_node tNode = rNode.append_child("w:t");
        tNode.append_attribute("xml:space") = "preserve";
        tNode.text().set("");
    }

    return pNode;
}

/// 将一个 RichTable 渲染为 <w:tbl> 元素
/// @param parent 父节点
/// @param table 表格数据
/// @return 创建的 <w:tbl> 节点
/// @note 样式: 全边框(0.5pt) + 列宽均分(A4 可用宽度) + 垂直居中;
///       表头行(hasHeadingRow)加粗 + 灰色底纹(D9D9D9) + 居中 + 跨页重复显示
pugi::xml_node CreateTableNode(pugi::xml_node parent, const RichTable& table) {
    // 列数以最大行为准(解析层已补齐, 这里兜底防御)
    size_t numCols = 1;
    for (const auto& row : table.rows) {
        numCols = std::max(numCols, row.size());
    }

    // A4 可用宽度: 11906 - 1800*2 = 8306 twips, 各列均分
    const int kTotalWidth = 8306;
    const int colWidth = kTotalWidth / static_cast<int>(numCols);

    pugi::xml_node tbl = parent.append_child("w:tbl");

    // 表格属性: 固定总宽 + 全边框
    pugi::xml_node tblPr = tbl.append_child("w:tblPr");
    pugi::xml_node tblW = tblPr.append_child("w:tblW");
    tblW.append_attribute("w:w") = kTotalWidth;
    tblW.append_attribute("w:type") = "dxa";
    pugi::xml_node tblBorders = tblPr.append_child("w:tblBorders");
    const char* edges[] = {"top", "left", "bottom", "right", "insideH", "insideV"};
    for (const char* edge : edges) {
        pugi::xml_node b = tblBorders.append_child((std::string("w:") + edge).c_str());
        b.append_attribute("w:val") = "single";
        b.append_attribute("w:sz") = 4;  // 0.5pt
        b.append_attribute("w:space") = 0;
        b.append_attribute("w:color") = "auto";
    }

    // 网格定义: 每列宽度
    pugi::xml_node tblGrid = tbl.append_child("w:tblGrid");
    for (size_t c = 0; c < numCols; ++c) {
        pugi::xml_node gridCol = tblGrid.append_child("w:gridCol");
        gridCol.append_attribute("w:w") = colWidth;
    }

    // 逐行渲染
    for (size_t r = 0; r < table.rows.size(); ++r) {
        const bool isHeading = table.hasHeadingRow && (r == 0);
        pugi::xml_node tr = tbl.append_child("w:tr");

        // 表头行属性: 跨页时重复显示
        if (isHeading) {
            pugi::xml_node trPr = tr.append_child("w:trPr");
            trPr.append_child("w:tblHeader");
        }

        const auto& row = table.rows[r];
        for (size_t c = 0; c < numCols; ++c) {
            pugi::xml_node tc = tr.append_child("w:tc");

            // 单元格属性: 宽度 + 垂直居中 + 表头底纹
            pugi::xml_node tcPr = tc.append_child("w:tcPr");
            pugi::xml_node tcW = tcPr.append_child("w:tcW");
            tcW.append_attribute("w:w") = colWidth;
            tcW.append_attribute("w:type") = "dxa";
            pugi::xml_node vAlign = tcPr.append_child("w:vAlign");
            vAlign.append_attribute("w:val") = "center";
            if (isHeading) {
                pugi::xml_node shd = tcPr.append_child("w:shd");
                shd.append_attribute("w:val") = "clear";
                shd.append_attribute("w:color") = "auto";
                shd.append_attribute("w:fill") = "D9D9D9";
            }

            // 单元格内容段落: 表头居中加粗, 正文左对齐
            RichParagraph cellPara;
            cellPara.alignment = isHeading ? "center" : "left";
            cellPara.lineSpacing = 1.0;
            if (c < row.size()) {
                cellPara.runs = row[c].runs;
            }
            if (isHeading) {
                for (auto& run : cellPara.runs) {
                    run.bold = true;
                }
            }
            CreateParagraphNode(tc, cellPara);
        }
    }

    return tbl;
}

/// 在 parent 末尾创建一个富文本块的全部节点
/// @param parent 父节点
/// @param para 富文本块(普通段落或表格)
/// @note 表格块创建 <w:tbl> + 后置空段落两个节点
///       (OOXML 规定表格后必须跟段落, 否则相邻表格会合并为一个); 普通块只创建一个 <w:p>
/// @note 需要 move 语义的调用方请先记录 parent.last_child(), 创建后移动新节点范围
void CreateBlockNodes(pugi::xml_node parent, const RichParagraph& para) {
    if (para.table) {
        CreateTableNode(parent, *para.table);
        // 表格后补空段落, 保证结构合法并隔开后续内容
        RichParagraph gap;
        CreateParagraphNode(parent, gap);
    } else {
        CreateParagraphNode(parent, para);
    }
}

} // anonymous namespace

void RenderParagraphsToXml(pugi::xml_node parent,
                           const std::vector<RichParagraph>& paragraphs,
                           pugi::xml_node placeholderParagraph) {
    // 将每个富文本块(段落或表格)渲染并插入到占位符段落之前
    for (const auto& para : paragraphs) {
        // 记录创建前的最后一个子节点, 用于界定本次新建节点的范围
        // (表格块会创建 <w:tbl> + 空段落两个节点)
        pugi::xml_node prevLast = parent.last_child();
        CreateBlockNodes(parent, para);

        // 将新建节点范围 [prevLast 之后, 末尾] 依次移动到占位符段落之前
        pugi::xml_node n = prevLast ? prevLast.next_sibling() : parent.first_child();
        while (n) {
            pugi::xml_node next = n.next_sibling();  // 先记录, move 后迭代失效
            parent.insert_move_before(n, placeholderParagraph);
            n = next;
        }
    }

    // 删除占位符段落
    parent.remove_child(placeholderParagraph);
}

std::string SerializeParagraphs(const std::vector<RichParagraph>& paragraphs) {
    // 创建临时文档，用 dummy root 节点作为父节点创建 <w:p>/<w:tbl> 元素
    pugi::xml_document doc;
    pugi::xml_node root = doc.append_child("root");

    for (const auto& para : paragraphs) {
        CreateBlockNodes(root, para);
    }

    // 序列化所有子节点（不含 dummy root）
    std::ostringstream oss;
    for (pugi::xml_node child = root.first_child(); child;
         child = child.next_sibling()) {
        child.print(oss, "", pugi::format_raw);
    }
    return oss.str();
}

void AppendParagraphsBefore(pugi::xml_node parent,
                             const std::vector<RichParagraph>& paragraphs,
                             pugi::xml_node beforeNode) {
    for (const auto& para : paragraphs) {
        // 记录创建前的最后一个子节点, 用于界定本次新建节点的范围
        // (表格块会创建 <w:tbl> + 空段落两个节点)
        pugi::xml_node prevLast = parent.last_child();
        CreateBlockNodes(parent, para);

        if (beforeNode) {
            // 将新建节点范围 [prevLast 之后, 末尾] 依次移动到 beforeNode 之前
            pugi::xml_node n = prevLast ? prevLast.next_sibling() : parent.first_child();
            while (n) {
                pugi::xml_node next = n.next_sibling();  // 先记录, move 后迭代失效
                parent.insert_move_before(n, beforeNode);
                n = next;
            }
        }
    }
}

} // namespace qifeng_ca::docx
