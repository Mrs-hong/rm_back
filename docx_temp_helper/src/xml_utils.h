/**
 * @file xml_utils.h
 * @brief XML 工具：基于 pugixml 的 OOXML 操作功能
 *
 * 提供对 OOXML document.xml 中 <w:r>（run）节点的文本收集与修改功能。
 * 内部使用 pugixml（MIT License）实现。
 */

#pragma once

#include <pugixml.hpp>
#include <string>
#include <vector>

namespace docx_temp_helper {

/// Run 信息结构体，记录一个 <w:r> 节点及其文本在段落全文中的位置
struct RunInfo {
    pugi::xml_node runNode;    ///< <w:r> 节点引用
    std::string text;          ///< <w:t> 文本内容
    size_t startPos;           ///< 该 run 文本在段落全文中的起始位置
    size_t endPos;             ///< 该 run 文本在段落全文中的结束位置
};

/// 收集段落中所有 <w:r> 子节点的文本信息
/// @param paragraph <w:p> 段落节点
/// @param outRuns   输出的 run 信息列表
/// @return 段落全文（所有 run 文本的拼接）
/// @note 跳过非 <w:r> 的子节点（如 <w:pPr>、<w:proofErr> 等），
///       但不修改它们，保证后续替换时格式不被破坏
std::string CollectRuns(pugi::xml_node paragraph, std::vector<RunInfo>& outRuns);

/// 设置 <w:r> 节点中 <w:t> 的文本内容
/// @param runNode  <w:r> 节点
/// @param text     新文本内容
/// @note 如果 <w:t> 不存在则创建；保留 <w:rPr> 不变
void SetRunText(pugi::xml_node runNode, const std::string& text);

/// 获取 <w:r> 节点中 <w:t> 的文本内容
/// @param runNode <w:r> 节点
/// @return 文本内容，若 <w:t> 不存在则返回空字符串
std::string GetRunText(pugi::xml_node runNode);

} // namespace docx_temp_helper
