/**
 * @file docx_document.h
 * @brief DOCX 模板占位符替换库 - class API
 *
 * 提供对 .docx 文件中 {{xx}} 占位符的文本/富文本替换功能，
 * 不改变原有排版样式（字体、字号、段落结构等）。
 *
 * 核心特性：
 *   - 面向对象设计：DocxDocument 管理文档生命周期，支持多次替换后一次保存
 *   - 支持通配符 {{*}} 匹配所有占位符
 *   - 支持精确匹配 {{会议主题}} 替换特定占位符
 *   - 支持批量替换（map<string,string> 一次传入多个）
 *   - 保留 OOXML 中 <w:r> / <w:rPr> 的完整格式
 *   - 富文本替换：HTML/Markdown → GB/T 9704-2012 格式化段落
 *   - 流式处理：当文件 + 替换内容总大小超过内存限制时自动启用
 *   - 详细的错误码与错误信息
 *
 * 错误处理统一使用 utils::Result<T>（由 utils/common 提供）。
 * 本域 ErrorCode 按 1xxx(IO) / 2xxx(XML/处理) 分区，与 audio 域保持一致的分区风格。
 *
 * 使用示例（纯文本）：
 * @code
 *   utils::docx::DocxConfig config;
 *   config.verbose = true;
 *   utils::docx::DocxDocument doc(config);
 *   if (!doc.Open("input.docx").IsOk()) return;
 *   auto result = doc.ReplaceText("{{*}}", "小红有才");
 *   if (result.IsOk()) doc.Save("output.docx");
 *   doc.Close();
 * @endcode
 *
 * 使用示例（富文本混合替换）：
 * @code
 *   utils::docx::DocxDocument doc;
 *   doc.Open("input.docx");
 *   std::map<std::string, utils::docx::RichReplacement> rich;
 *   rich["正文"] = {mdContent, utils::docx::ContentType::Markdown};
 *   rich["会议主题"] = {"年终总结", utils::docx::ContentType::Plain};
 *   doc.ReplaceRich(rich);
 *   doc.Save("output.docx");
 * @endcode
 */

#pragma once

#include "utils/docx/rich_content.h"

#include "utils/common/Result.hpp"

#include <pugixml.hpp>

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace utils::docx {

// ───────── 错误码与错误信息 ─────────

/// 错误码枚举（按域分区：1xxx=文件/IO，2xxx=XML/处理）
enum class ErrorCode : int {
  Ok = 0,

  // ── 1xxx: 文件 / IO ──
  FileNotFound         = 1001, ///< 输入文件不存在
  FileNotReadable      = 1002, ///< 输入文件无法读取
  InvalidDocxFormat    = 1003, ///< 非合法 docx 文件（zip 结构损坏）
  UnzipFailed          = 1004, ///< 解压失败
  TempDirCreateFailed  = 1005, ///< 临时目录创建失败
  OutputWriteFailed    = 1006, ///< 输出文件写入失败

  // ── 2xxx: XML / 处理 ──
  XmlParseFailed       = 2001, ///< XML 解析失败
  XmlSaveFailed        = 2002, ///< XML 保存失败
  ZipFailed            = 2003, ///< 压缩失败
  NoMatchFound         = 2004, ///< 未找到匹配的占位符
  InvalidPattern       = 2005, ///< 占位符模式格式非法
  NotOpened            = 2006, ///< 文档未打开（未调用 Open 或已 Close）
  UnknownError         = 2099, ///< 未知错误
};

/// 返回错误码对应的简短描述
/// @param code 错误码（接受 int 以兼容 utils::Result<T>::Code() 的返回值）
/// @return 错误码对应的人可读简短描述（英文标识，与枚举名一致）
const char* ToMessage(int code);

// 引入 utils::Result 模板到 utils::docx 域，使本域内可直接使用非限定名 Result<T>
using utils::Result;

// ───────── 替换结果 ─────────

/// 单个占位符的替换记录
struct ReplaceRecord {
  std::string placeholder; ///< 原始占位符，如 "{{会议主题}}"
  std::string replacement; ///< 替换后的文本
  int count = 0; ///< 替换次数（同一占位符可能出现多次）
};

/// 替换统计信息（作为 Result<ReplaceStats> 的值载体）
/// @details 承载原 ReplaceResult 中的 records 与 totalReplaced 字段，
///          通过 Result<ReplaceStats> 返回时同时携带错误状态与统计数据
struct ReplaceStats {
  std::vector<ReplaceRecord> records; ///< 逐个占位符的替换记录
  int totalReplaced = 0;              ///< 总替换次数
};

// ───────── 配置 ─────────

/// DocxDocument 配置项
struct DocxConfig {
  /// 内存限制（字节）。当 docx 文件大小 + 替换内容总大小超过此值时启用流式处理
  /// 默认 10MB，可按需调整
  size_t memoryLimit = 10 * 1024 * 1024;

  std::string tempDir; ///< 临时解压目录，为空则使用系统临时目录
  bool verbose = false; ///< 是否输出详细日志
  bool keepTempDir = false; ///< 是否保留临时目录（调试用，默认清理）
};

// ───────── 主类 ─────────

/// DOCX 文档处理器
///
/// 管理 docx 文档的打开、替换、保存、关闭生命周期。
/// 内部自动根据文件大小和内存限制选择 DOM 模式或流式模式：
///   - DOM 模式：全量加载 document.xml 到内存，支持多次替换复用 DOM
///   - 流式模式：按段落逐个处理，内存占用 O(最大段落大小)
class DocxDocument {
public:
  /// 默认构造（使用默认配置）
  DocxDocument();

  /// 使用指定配置构造
  explicit DocxDocument(const DocxConfig &config);

  /// 析构时自动清理临时目录
  ~DocxDocument();

  // 禁拷贝（持有临时目录等资源）
  DocxDocument(const DocxDocument &) = delete;
  DocxDocument &operator=(const DocxDocument &) = delete;

  // 允许移动
  DocxDocument(DocxDocument &&) = default;
  DocxDocument &operator=(DocxDocument &&) = default;

  // ── 生命周期管理 ──

  /// 打开 docx 文件（解压到临时目录）
  /// @param path docx 文件路径
  /// @return Result<void>（ok=成功）
  Result<void> Open(const std::string &path);

  /// 保存文档到指定路径（重新压缩为 docx）
  /// @param path 输出 docx 文件路径
  /// @return Result<void>（ok=成功）
  Result<void> Save(const std::string &path);

  /// 关闭文档并清理临时目录
  void Close();

  // ── 文本替换 ──

  /// 批量纯文本替换：传入 map<string, string> 一次替换多个占位符
  /// @param replacements 占位符名称→替换文本 的映射
  ///                       key 为占位符名称（不含 {{}}），如 "会议主题"
  ///                       value 为替换文本，如 "小红有才"
  ///                       若 key 为 "*"，则匹配所有 {{xx}} 并统一替换
  /// @return Result<ReplaceStats>（含错误状态与逐项替换记录）
  Result<ReplaceStats>
  ReplaceText(const std::map<std::string, std::string> &replacements);

  /// 单模式纯文本替换
  /// @param pattern     占位符模式，支持 "{{*}}"（匹配所有）或 "{{具体名称}}"
  /// @param replacement 替换文本
  /// @return Result<ReplaceStats>（含错误状态与逐项替换记录）
  Result<ReplaceStats> ReplaceText(const std::string &pattern,
                            const std::string &replacement);

  // ── 富文本替换 ──

  /// 富文本批量替换：支持纯文本/HTML/Markdown 混合替换
  /// @param replacements 占位符名称→富文本替换项 的映射
  ///                       RichReplacement.type 决定内容类型：
  ///                         - Plain: 走纯文本替换
  ///                         - HTML: 解析 HTML 并生成格式化段落
  ///                         - Markdown: 解析 MD 并生成格式化段落
  ///                       HTML/Markdown 仅替换独占一行的占位符（替换整个段落）
  /// @return Result<ReplaceStats>（含错误状态与逐项替换记录）
  Result<ReplaceStats>
  ReplaceRich(const std::map<std::string, RichReplacement> &replacements);

  // ── 文档生成（从空白模板）──

  /// 从空白模板生成文档（不依赖占位符）
  ///
  /// 在已打开的空白模板中插入标题和正文内容，生成完整文档。
  /// 标题以 h1 格式渲染（方正小标宋简体 22pt 居中），
  /// 正文根据 contentType 渲染为 GB/T 9704-2012 格式化段落。
  ///
  /// @param title 标题（可选，为空则不添加标题）
  /// @param bodyContent 正文内容
  /// @param contentType 内容类型（Plain/HTML/Markdown）
  /// @return Result<ReplaceStats>（含错误状态与生成记录）
  ///
  /// @note 需先调用 Open() 打开空白模板，生成后调用 Save() 保存
  /// @note Plain 类型按行分割，每行渲染为仿宋正文段落
  /// @note HTML/Markdown 类型使用富文本解析器生成格式化段落
  Result<ReplaceStats> GenerateDocument(const std::string &title,
                                 const std::string &bodyContent,
                                 ContentType contentType);

  // ── 状态查询 ──

  /// 文档是否已打开
  bool IsOpen() const;

  /// 是否处于流式处理模式
  bool IsStreamingMode() const;

  /// 获取已打开文件的大小（字节）
  size_t GetFileSize() const;

  /// 获取配置
  const DocxConfig &Config() const;

private:
  // ── 内部状态 ──
  DocxConfig mConfig;
  std::string mTempDir;        ///< 临时解压目录
  std::string mInputPath;      ///< 输入 docx 路径
  size_t mFileSize = 0;        ///< 输入文件大小
  bool mStreamingMode = false; ///< 是否使用流式模式
  bool mOpened = false;        ///< 是否已打开
  bool mDomLoaded = false;     ///< DOM 是否已加载

  // DOM 模式数据
  pugi::xml_document mDomDoc; ///< document.xml 的 DOM 树

  // ── 内部工具方法 ──

  /// 判断是否应使用流式模式
  /// @param extraSize 替换内容总大小
  bool ShouldUseStreaming_(size_t extraSize) const;

  /// 解压 docx 到 mTempDir
  Result<void> Unzip_();

  /// 加载 document.xml 到 mDomDoc
  Result<void> ParseXmlDom_();

  /// 保存 mDomDoc 到 document.xml
  Result<void> SaveXmlDom_();

  /// 将 mTempDir 压缩为 docx
  Result<void> Zip_(const std::string &outputPath);

  /// 创建临时目录
  Result<void> CreateTempDir_();

  /// 清理临时目录
  void CleanupTempDir_();

  // ── DOM 模式替换 ──

  Result<ReplaceStats>
  ReplaceTextDom_(const std::map<std::string, std::string> &replacements);
  Result<ReplaceStats>
  ReplaceRichDom_(const std::map<std::string, RichReplacement> &replacements);

  // ── 流式模式替换 ──

  Result<ReplaceStats>
  ReplaceTextStreaming_(const std::map<std::string, std::string> &replacements);
  Result<ReplaceStats> ReplaceRichStreaming_(
      const std::map<std::string, RichReplacement> &replacements);
};

// ───────── ZIP 工具函数（复用错误码）─────────

/// 将目录或文件压缩为 ZIP
/// @param srcPath    源路径（目录或文件均可）
/// @param outputPath 输出目录路径（不存在则自动创建）
/// @param zipName    ZIP 包名（如 "archive.zip"）
/// @return Result<void>（ok=成功）
/// @note 若 srcPath 为目录，递归打包其下所有文件，保持目录结构
/// @note 若 srcPath 为文件，打包该单个文件（ZIP 内仅含文件名，不含目录路径）
/// @note 最终输出路径为 outputPath/zipName
Result<void> ZipCompress(const std::string& srcPath,
                        const std::string& outputPath,
                        const std::string& zipName);

/// 解压 ZIP 文件到指定目录
/// @param zipPath  ZIP 文件路径
/// @param destDir  解压目标目录（不存在则自动创建）
/// @return Result<void>（ok=成功）
Result<void> ZipExtract(const std::string& zipPath,
                        const std::string& destDir);

} // namespace utils::docx
