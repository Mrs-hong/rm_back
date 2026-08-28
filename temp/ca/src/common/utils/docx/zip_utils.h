//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

/**
 * @file zip_utils.h
 * @brief ZIP 工具：基于 minizip-ng 的解压/压缩功能
 *
 * 提供 docx（ZIP 格式）文件的解压和重新压缩功能。
 * 内部使用 minizip-ng 的向后兼容层(zip.h/unzip.h/ioapi.h, zlib License)实现。
 * 头文件不暴露任何 minizip 类型, 仅依赖 std::string/bool, 实现可平滑替换。
 */

#pragma once

#include <string>
#include <vector>

namespace qifeng_ca::docx {

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

/// 将指定文件列表压缩为 ZIP
/// @param filePaths 源文件绝对路径列表
/// @param zipPath   输出 ZIP 文件路径
/// @param storeOnly true=仅存储不压缩（Store 方法），false=使用 Deflate 压缩
/// @return true=成功, false=失败
/// @note ZIP 内仅包含列表中的文件(取 basename, 不含目录结构);
///       用于从共享缓存目录中挑选指定文件打包, 避免把目录内其他文件一起打入
bool ZipFiles(const std::vector<std::string>& filePaths, const std::string& zipPath,
              bool storeOnly = false);

} // namespace qifeng_ca::docx
