/**
 * @file xml_utils.cpp
 * @brief XML 工具实现：基于 pugixml 的 OOXML 操作功能
 */

#include "xml_utils.h"

#include <cstring>

namespace docx_temp_helper {

std::string collectRuns(pugi::xml_node paragraph, std::vector<RunInfo>& outRuns) {
    outRuns.clear();
    std::string fullText;
    size_t currentPos = 0;

    // 遍历段落的所有直接子节点
    for (pugi::xml_node child = paragraph.first_child(); child;
         child = child.next_sibling()) {
        // 只处理 <w:r> 节点（带 w 命名空间的 run 元素）
        if (strcmp(child.name(), "w:r") != 0) {
            continue;
        }

        // 在 <w:r> 中查找 <w:t> 节点
        pugi::xml_node tNode = child.child("w:t");
        std::string text;
        if (tNode) {
            text = tNode.text().get();
        }

        // 记录 run 信息
        RunInfo info;
        info.runNode = child;
        info.text = text;
        info.startPos = currentPos;
        info.endPos = currentPos + text.size();
        outRuns.push_back(info);

        fullText += text;
        currentPos += text.size();
    }

    return fullText;
}

void setRunText(pugi::xml_node runNode, const std::string& text) {
    // 查找已有的 <w:t> 节点
    pugi::xml_node tNode = runNode.child("w:t");

    if (!tNode) {
        // <w:t> 不存在，在 <w:r> 中创建
        // 保留 <w:rPr> 在前面（如果存在），<w:t> 放在 <w:rPr> 之后
        pugi::xml_node rPr = runNode.child("w:rPr");
        if (rPr) {
            tNode = runNode.insert_child_after("w:t", rPr);
        } else {
            // 没有 <w:rPr>，<w:t> 作为第一个子节点
            tNode = runNode.prepend_child("w:t");
        }
    }

    // 设置文本内容
    // xml:space="preserve" 确保空格不被忽略
    tNode.text().set(text.c_str());
    tNode.remove_attribute("xml:space");
    tNode.append_attribute("xml:space") = "preserve";
}

std::string getRunText(pugi::xml_node runNode) {
    pugi::xml_node tNode = runNode.child("w:t");
    if (tNode) {
        return tNode.text().get();
    }
    return "";
}

} // namespace docx_temp_helper
