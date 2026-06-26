/**
 * @file zip_utils.h
 * @brief ZIP 工具：基于 minizip 的解压/压缩功能
 *
 * 提供 docx（ZIP 格式）文件的解压和重新压缩功能。
 * 内部使用 minizip（zlib License）实现。
 */

#pragma once

#include <string>

namespace docx_temp_helper {

/// 解压 ZIP/docx 文件到指定目录
/// @param zipPath  ZIP/docx 文件路径
/// @param destDir  目标目录（需已存在）
/// @return true=成功, false=失败
bool unzipToDir(const std::string& zipPath, const std::string& destDir);

/// 将目录压缩为 ZIP/docx 文件
/// @param srcDir   源目录
/// @param zipPath  输出 ZIP/docx 文件路径
/// @return true=成功, false=失败
/// @note docx 要求 [Content_Types].xml 在 ZIP 中，本函数保持目录结构
bool zipDir(const std::string& srcDir, const std::string& zipPath);

} // namespace docx_temp_helper
