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

#include "docx_temp_helper/rich_content.h"

#include <pugixml.hpp>

#include <cstring>
#include <sstream>
#include <algorithm>
#include <regex>

namespace docx_temp_helper {

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
const char* getHeadingFont(int level) {
    switch (level) {
        case 1:  return FONT_TITLE;
        case 2:  return FONT_HEADING;
        case 3:  return FONT_SUBHEAD;
        default: return FONT_BODY;
    }
}

/// 获取标题级别对应的字号（半磅）
int getHeadingSize(int level) {
    return (level == 1) ? SIZE_H1 : SIZE_BODY;
}

/// 获取标题级别对应的对齐方式
const char* getHeadingAlignment(int level) {
    return (level == 1) ? "center" : "left";
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Markdown 解析器
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

/// 解析行内格式（加粗、斜体）为 RichRun 列表
/// 支持: **bold**, *italic*, ***bold+italic***
std::vector<RichRun> parseInlineMarkdown(const std::string& text) {
    std::vector<RichRun> runs;

    // 正则匹配 **bold**, *italic*, ***bold+italic***
    // 按顺序匹配：先 ***...***, 再 **...**, 再 *...*
    std::regex pattern(R"((\*\*\*(.+?)\*\*\*)|(\*\*(.+?)\*\*)|(\*(.+?)\*))");
    std::smatch match;
    size_t lastEnd = 0;
    std::string remaining = text;

    size_t pos = 0;
    while (pos < text.size()) {
        bool matched = false;

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
                matched = true;
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
                matched = true;
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
                matched = true;
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
std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

/// 检查字符串是否以指定前缀开头
bool startsWith(const std::string& s, const char* prefix) {
    return s.compare(0, strlen(prefix), prefix) == 0;
}

} // anonymous namespace

std::vector<RichParagraph> parseMarkdown(const std::string& md) {
    std::vector<RichParagraph> paragraphs;

    std::istringstream stream(md);
    std::string line;

    int orderedListIndex = 0;  // 有序列表计数器

    while (std::getline(stream, line)) {
        std::string trimmed = trim(line);

        // 空行：跳过（段落分隔符）
        if (trimmed.empty()) {
            orderedListIndex = 0;  // 重置有序列表计数
            continue;
        }

        RichParagraph para;

        // 检查标题: # ~ ####
        if (startsWith(trimmed, "#### ")) {
            para.headingLevel = 4;
            std::string text = trimmed.substr(5);
            para.runs = parseInlineMarkdown(text);
        } else if (startsWith(trimmed, "### ")) {
            para.headingLevel = 3;
            std::string text = trimmed.substr(4);
            para.runs = parseInlineMarkdown(text);
        } else if (startsWith(trimmed, "## ")) {
            para.headingLevel = 2;
            std::string text = trimmed.substr(3);
            para.runs = parseInlineMarkdown(text);
        } else if (startsWith(trimmed, "# ")) {
            para.headingLevel = 1;
            std::string text = trimmed.substr(2);
            para.runs = parseInlineMarkdown(text);
        }
        // 检查无序列表: - 或 *
        else if (startsWith(trimmed, "- ") || startsWith(trimmed, "* ")) {
            para.isListItem = true;
            para.isOrdered = false;
            std::string text = trimmed.substr(2);
            para.runs = parseInlineMarkdown(text);
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
                    para.runs = parseInlineMarkdown(text);
                } else {
                    // 普通段落
                    orderedListIndex = 0;
                    para.runs = parseInlineMarkdown(trimmed);
                }
            } else {
                // 普通段落
                orderedListIndex = 0;
                para.runs = parseInlineMarkdown(trimmed);
            }
        }

        // 设置默认格式
        if (para.headingLevel > 0) {
            // 标题格式
            para.alignment = getHeadingAlignment(para.headingLevel);
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
std::vector<RichRun> parseHtmlInline(pugi::xml_node node,
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
                auto childRuns = parseHtmlInline(child, true, inheritedItalic, inheritedUnderline);
                runs.insert(runs.end(), childRuns.begin(), childRuns.end());
            } else if (tagName == "i" || tagName == "em") {
                // 斜体
                auto childRuns = parseHtmlInline(child, inheritedBold, true, inheritedUnderline);
                runs.insert(runs.end(), childRuns.begin(), childRuns.end());
            } else if (tagName == "u") {
                // 下划线
                auto childRuns = parseHtmlInline(child, inheritedBold, inheritedItalic, true);
                runs.insert(runs.end(), childRuns.begin(), childRuns.end());
            } else if (tagName == "br") {
                // 换行：插入空 run 作为分隔（实际渲染时可以忽略）
                RichRun r;
                r.text = "\n";
                runs.push_back(r);
            } else if (tagName == "span") {
                // span：直接递归（忽略 style 属性中的格式）
                auto childRuns = parseHtmlInline(child, inheritedBold, inheritedItalic, inheritedUnderline);
                runs.insert(runs.end(), childRuns.begin(), childRuns.end());
            } else {
                // 其他标签：递归提取文本
                auto childRuns = parseHtmlInline(child, inheritedBold, inheritedItalic, inheritedUnderline);
                runs.insert(runs.end(), childRuns.begin(), childRuns.end());
            }
        }
    }

    return runs;
}

/// 从 HTML 元素创建段落
RichParagraph createHtmlParagraph(pugi::xml_node element, int headingLevel = 0) {
    RichParagraph para;
    para.headingLevel = headingLevel;
    para.runs = parseHtmlInline(element);

    if (headingLevel > 0) {
        para.alignment = getHeadingAlignment(headingLevel);
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

std::vector<RichParagraph> parseHtml(const std::string& html) {
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
            paragraphs.push_back(createHtmlParagraph(element, 1));
        } else if (tagName == "h2") {
            paragraphs.push_back(createHtmlParagraph(element, 2));
        } else if (tagName == "h3") {
            paragraphs.push_back(createHtmlParagraph(element, 3));
        } else if (tagName == "h4" || tagName == "h5" || tagName == "h6") {
            paragraphs.push_back(createHtmlParagraph(element, 4));
        } else if (tagName == "p") {
            paragraphs.push_back(createHtmlParagraph(element, 0));
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
                para.runs = parseHtmlInline(li);

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
            auto runs = parseHtmlInline(element);
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
void createRunProperties(pugi::xml_node runNode,
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
void createParagraphProperties(pugi::xml_node pPr, const RichParagraph& para) {
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
pugi::xml_node createParagraphNode(pugi::xml_node parent, const RichParagraph& para) {
    pugi::xml_node pNode = parent.append_child("w:p");

    // 段落属性
    pugi::xml_node pPr = pNode.append_child("w:pPr");
    createParagraphProperties(pPr, para);

    // 确定字体和字号
    const char* font = FONT_BODY;
    int size = SIZE_BODY;
    if (para.headingLevel > 0) {
        font = getHeadingFont(para.headingLevel);
        size = getHeadingSize(para.headingLevel);
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
        createRunProperties(rNode, font, size, run.bold, run.italic, run.underline);

        pugi::xml_node tNode = rNode.append_child("w:t");
        tNode.append_attribute("xml:space") = "preserve";
        tNode.text().set(text.c_str());
    }

    // 如果没有 runs，添加一个空 run 以保证段落不为空
    if (pNode.child("w:r") == nullptr) {
        pugi::xml_node rNode = pNode.append_child("w:r");
        createRunProperties(rNode, font, size, false, false, false);
        pugi::xml_node tNode = rNode.append_child("w:t");
        tNode.append_attribute("xml:space") = "preserve";
        tNode.text().set("");
    }

    return pNode;
}

} // anonymous namespace

void renderParagraphsToXml(pugi::xml_node parent,
                           const std::vector<RichParagraph>& paragraphs,
                           pugi::xml_node placeholderParagraph) {
    // 将每个 RichParagraph 渲染为 <w:p> 并插入到占位符段落之前
    for (const auto& para : paragraphs) {
        // 创建新段落（先 append 到 parent 末尾）
        pugi::xml_node newPara = createParagraphNode(parent, para);
        // 移动到占位符段落之前
        parent.insert_move_before(newPara, placeholderParagraph);
    }

    // 删除占位符段落
    parent.remove_child(placeholderParagraph);
}

} // namespace docx_temp_helper
