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
bool UnzipToDir(const std::string& zipPath, const std::string& destDir);

/// 将目录压缩为 ZIP/docx 文件
/// @param srcDir   源目录
/// @param zipPath  输出 ZIP/docx 文件路径
/// @param storeOnly true=仅存储不压缩（Store 方法），false=使用 Deflate 压缩
/// @return true=成功, false=失败
/// @note docx 要求 [Content_Types].xml 在 ZIP 中，本函数保持目录结构
bool ZipDir(const std::string& srcDir, const std::string& zipPath,
            bool storeOnly = false);

/// 将单个文件压缩为 ZIP
/// @param filePath 源文件路径
/// @param zipPath  输出 ZIP 文件路径
/// @param storeOnly true=仅存储不压缩（Store 方法），false=使用 Deflate 压缩
/// @return true=成功, false=失败
/// @note ZIP 内仅包含该文件（不含目录结构）
bool ZipSingleFile(const std::string& filePath, const std::string& zipPath,
                   bool storeOnly = false);

} // namespace docx_temp_helper
