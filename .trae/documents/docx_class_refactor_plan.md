# 重构计划：docx_temp_helper 改为 class 架构 + 流式处理

## 概述

将现有自由函数 API 完全替换为 `DocxDocument` class，新增可配置内存限制的流式处理模式，
并提供 `test/` 目录下的完整独立测试程序（含测试数据）。

---

## 当前状态分析

### 现有文件清单

| 文件 | 行数 | 职责 | 重构动作 |
|------|------|------|---------|
| `include/docx_temp_helper/docx_replacer.h` | 150 | 自由函数 API | **删除**，替换为 `docx_document.h` |
| `include/docx_temp_helper/rich_content.h` | 97 | 富文本类型 | **保留**，新增 `serializeParagraphs` 函数 |
| `src/docx_replacer.cpp` | 705 | 主实现 | **删除**，逻辑迁移到 `docx_document.cpp` |
| `src/rich_content.cpp` | 653 | MD/HTML 解析+渲染 | **保留**，新增 `serializeParagraphs` 实现 |
| `src/zip_utils.h/.cpp` | ~200 | ZIP 工具 | **保留不动** |
| `src/xml_utils.h/.cpp` | ~150 | XML 工具 | **保留不动** |
| `CMakeLists.txt` | 60 | 构建配置 | **更新**：源文件列表+测试目标 |
| `main.cpp` | 135 | CLI 工具 | **更新**：改用 class API |

### 当前内存模式

- ZIP I/O：已分块（8KB 缓冲区）
- XML DOM：`pugi::xml_document::load_file` 全量加载到内存
- 典型 `document.xml`：~40KB，总 docx ~100KB
- 无流式处理能力

---

## 重构方案

### 1. 新增头文件 `include/docx_temp_helper/docx_document.h`

**完全替换** `docx_replacer.h`，包含 class API + 所有类型定义。

```cpp
// 错误类型（从 docx_replacer.h 迁移）
enum class ErrorCode { Ok, FileNotFound, ... };
struct ErrorInfo { ErrorCode code; std::string message; std::string detail; ... };
struct ReplaceRecord { std::string placeholder, replacement; int count; };
struct ReplaceResult { ErrorInfo error; std::vector<ReplaceRecord> records; int totalReplaced; ... };

// 配置（新增）
struct DocxConfig {
    size_t memoryLimit = 10 * 1024 * 1024;  // 10MB，可配置
    std::string tempDir;                     // 空=系统临时目录
    bool verbose = false;
    bool keepTempDir = false;
};

// 主类
class DocxDocument {
public:
    DocxDocument();
    explicit DocxDocument(const DocxConfig& config);
    ~DocxDocument();

    // 禁拷贝，允许移动
    DocxDocument(const DocxDocument&) = delete;
    DocxDocument& operator=(const DocxDocument&) = delete;
    DocxDocument(DocxDocument&&) = default;
    DocxDocument& operator=(DocxDocument&&) = default;

    // 生命周期
    ErrorInfo open(const std::string& path);
    ErrorInfo save(const std::string& path);
    void close();

    // 文本替换
    ReplaceResult replaceText(const std::map<std::string, std::string>& replacements);
    ReplaceResult replaceText(const std::string& pattern, const std::string& replacement);

    // 富文本替换
    ReplaceResult replaceRich(const std::map<std::string, RichReplacement>& replacements);

    // 状态查询
    bool isOpen() const;
    bool isStreamingMode() const;
    size_t getFileSize() const;
    const DocxConfig& config() const;

private:
    DocxConfig config_;
    std::string tempDir_;
    std::string inputPath_;
    size_t fileSize_ = 0;
    bool streamingMode_ = false;
    bool opened_ = false;

    // DOM 模式
    pugi::xml_document domDoc_;

    // 内部方法
    bool shouldUseStreaming_(size_t extraSize) const;
    ErrorInfo unzip_();
    ErrorInfo parseXmlDom_();
    ErrorInfo saveXmlDom_();
    ErrorInfo zip_(const std::string& outputPath);

    // DOM 模式替换
    ReplaceResult replaceTextDom_(const std::map<std::string, std::string>& replacements);
    ReplaceResult replaceRichDom_(const std::map<std::string, RichReplacement>& replacements);

    // 流式模式替换
    ReplaceResult replaceTextStreaming_(const std::map<std::string, std::string>& replacements);
    ReplaceResult replaceRichStreaming_(const std::map<std::string, RichReplacement>& replacements);
};
```

### 2. 新增 `src/streaming_processor.h/.cpp`（内部头文件，不对外暴露）

流式 XML 处理器，按段落逐个处理 `document.xml`。

```cpp
// 内部类，不在公开头文件中
namespace docx_temp_helper::detail {

/// 段落处理器回调函数类型
/// @param paraXml 段落原始 XML 字符串（完整的 <w:p>...</w:p>）
/// @return 处理后的 XML 字符串；若返回空串则保持原文不变
using ParagraphHandler = std::function<std::string(const std::string& paraXml)>;

/// 流式处理 document.xml
/// 按 <w:p>...</w:p> 边界逐段读取，对每段调用 handler
/// 段落外的 XML 内容（header, <w:body>, <w:sectPr> 等）直接透传
/// @param inputXmlPath  输入 XML 文件路径
/// @param outputXmlPath 输出 XML 文件路径
/// @param handler       段落处理回调
/// @return true=成功
bool streamProcessXml(const std::string& inputXmlPath,
                      const std::string& outputXmlPath,
                      ParagraphHandler handler);

} // namespace detail
```

**流式处理算法**：
1. 以 64KB 块读取 `document.xml`
2. 在缓冲区中扫描 `<w:p ` 或 `<w:p>` 标签起始
3. 继续读取直到找到 `</w:p>` 标签结束
4. 提取完整段落 XML 字符串
5. 用 pugixml 解析为 fragment，执行替换逻辑
6. 序列化修改后的段落写入输出文件
7. 段落外内容（XML 声明、`<w:body>`、`<w:sectPr>` 等）直接透传
8. 内存复杂度：O(最大单个段落大小)，典型 < 100KB

### 3. 新增 `rich_content.h` 中的 `serializeParagraphs` 函数

流式模式下无法使用 `renderParagraphsToXml`（需要 parent 节点），新增序列化函数：

```cpp
/// 将段落列表序列化为 XML 字符串（不依赖 parent 节点）
/// @param paragraphs 段落列表
/// @return XML 字符串，包含多个 <w:p> 元素
std::string serializeParagraphs(const std::vector<RichParagraph>& paragraphs);
```

### 4. 新增 `src/docx_document.cpp`（主实现）

从 `docx_replacer.cpp` 迁移核心逻辑，重组为 class 方法。

**关键实现**：

#### `open(path)`
1. 检查文件存在
2. 记录 `fileSize_`（`fs::file_size`）
3. 创建临时目录
4. `unzipToDir(path, tempDir_)`

#### `shouldUseStreaming_(extraSize)`
```cpp
return fileSize_ + extraSize > config_.memoryLimit;
```

#### `replaceText(map)` / `replaceRich(map)`
1. 计算替换内容总大小 `extraSize`
2. 调用 `shouldUseStreaming_` 判断模式
3. 调用 `replaceTextDom_` 或 `replaceTextStreaming_`
   - DOM 模式：首次调用时 `parseXmlDom_()` 加载 DOM，后续复用
   - 流式模式：每次调用都流式处理 `document.xml`

#### DOM 模式 `replaceTextDom_`
- 复用现有 `replaceInParagraph` 逐字符法
- 复用现有 `traverseNodes` 递归遍历
- 首次调用时加载 DOM，后续调用直接操作已加载的 DOM

#### 流式模式 `replaceTextStreaming_`
1. 调用 `detail::streamProcessXml(document.xml, document_out.xml, handler)`
2. handler 内部：
   a. 用 pugixml 解析段落 fragment
   b. 调用 `replaceInParagraph` 处理文本替换
   c. 序列化修改后的段落返回
3. 替换完成后将 `document_out.xml` 重命名为 `document.xml`

#### `save(path)`
1. DOM 模式：`saveXmlDom_()` 保存 DOM 到文件 → `zip_(path)`
2. 流式模式：`document.xml` 已在流式处理中更新 → 直接 `zip_(path)`

#### `close()`
1. 清理临时目录（除非 `keepTempDir`）
2. 重置状态

### 5. 更新 `main.cpp`

改用 class API：
```cpp
docx_temp_helper::DocxConfig config;
config.verbose = true;
docx_temp_helper::DocxDocument doc(config);
doc.open(inputPath);
doc.replaceText("{{*}}", "小红有才");
doc.save(outputPath);
doc.close();
```

### 6. 测试程序

在 `test/` 目录下创建独立测试程序和测试数据。

**测试目录结构**：
```
test/
├── test_plain.cpp         # 纯文本替换测试
├── test_markdown.cpp      # Markdown 替换测试
├── test_html.cpp          # HTML 替换测试
├── test_mixed.cpp         # 混合替换测试
├── test_streaming.cpp     # 流式处理测试
├── test_error.cpp         # 错误处理测试
└── data/
    ├── template.docx      # 测试模板（复制自现有模板）
    ├── content.md         # Markdown 测试内容
    └── content.html       # HTML 测试内容
```

**各测试程序内容**：

#### `test_plain.cpp`
- 打开 template.docx
- 批量替换 6 个占位符（会议主题、会议时间、会议地点、主持人、参会人员、备注）
- 验证：`totalReplaced == 6`，每个 records 项正确
- 验证：输出文件存在且可解压

#### `test_markdown.cpp`
- 打开 template.docx
- 读取 `test/data/content.md` 内容
- 用 Markdown 替换 `{{正文}}`
- 验证：`totalReplaced >= 1`
- 验证：输出 docx 解压后 `document.xml` 包含"会议内容"等 MD 中的文本

#### `test_html.cpp`
- 打开 template.docx
- 读取 `test/data/content.html` 内容
- 用 HTML 替换 `{{正文}}`
- 验证：`totalReplaced >= 1`
- 验证：输出 docx 解压后 `document.xml` 包含 `<w:b>` 加粗标记

#### `test_mixed.cpp`
- 打开 template.docx
- 纯文本替换会议主题/主持人 + Markdown 替换正文
- 验证：所有替换都成功

#### `test_streaming.cpp`
- 打开 template.docx（设置 `memoryLimit = 1024` 即 1KB，强制触发流式模式）
- 执行纯文本替换
- 验证：`doc.isStreamingMode() == true`
- 验证：替换结果正确（与 DOM 模式结果一致）
- 同时测试流式模式下的 Markdown 替换

#### `test_error.cpp`
- 测试文件不存在 → `ErrorCode::FileNotFound`
- 测试空替换映射 → `ErrorCode::InvalidPattern`
- 测试无匹配占位符 → `ErrorCode::NoMatchFound`
- 测试未 open 就调用 replaceText → 返回错误

**测试数据文件**：

`test/data/content.md`:
```markdown
## 会议内容

本次会议讨论了以下重要事项：

- 项目进度汇报
- 下阶段工作安排
- 预算审批与调整

### 详细说明

**重要决定**：项目延期至下月执行。

1. 技术部完成架构升级
2. 产品部发布新版本
3. 运营部扩大市场推广
```

`test/data/content.html`:
```html
<h2>会议内容</h2>
<p>本次会议讨论了<b>项目进度</b>和<i>预算审批</i>。</p>
<ul>
<li>进度正常</li>
<li>预算超支</li>
</ul>
<h3>注意事项</h3>
<p>请各部门提前准备<b>汇报材料</b>。</p>
```

### 7. 更新 `CMakeLists.txt`

```cmake
# 主库源文件更新
add_library(docx_temp_helper STATIC
    src/docx_document.cpp        # 替换 docx_replacer.cpp
    src/streaming_processor.cpp  # 新增
    src/rich_content.cpp
    src/zip_utils.cpp
    src/xml_utils.cpp
)

# CLI 工具
add_executable(test_docx main.cpp)
target_link_libraries(test_docx PRIVATE docx_temp_helper)

# 测试程序
enable_testing()
set(TEST_SOURCES
    test/test_plain
    test/test_markdown
    test/test_html
    test/test_mixed
    test/test_streaming
    test/test_error
)
foreach(test ${TEST_SOURCES})
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE docx_temp_helper)
    add_test(NAME ${test} COMMAND ${test})
endforeach()
```

---

## 实施步骤

### 步骤 1：创建 `docx_document.h`
- 定义 `DocxConfig`、`DocxDocument` class
- 迁移 `ErrorCode`、`ErrorInfo`、`ReplaceRecord`、`ReplaceResult` 类型
- 删除 `docx_replacer.h`

### 步骤 2：创建 `streaming_processor.h/.cpp`
- 实现 `streamProcessXml` 函数
- 64KB 块读取 + 段落边界扫描 + 回调处理

### 步骤 3：创建 `docx_document.cpp`
- 从 `docx_replacer.cpp` 迁移 `replaceInParagraph`、`findPlaceholders`、`isParagraphOnlyPlaceholder` 等内部函数
- 实现 `DocxDocument` 的 open/save/close/replaceText/replaceRich
- 实现 DOM 模式和流式模式两条路径
- 删除 `docx_replacer.cpp`

### 步骤 4：更新 `rich_content.h/.cpp`
- 新增 `serializeParagraphs` 函数声明和实现
- 其他代码不动

### 步骤 5：更新 `main.cpp`
- 改用 `DocxDocument` class API

### 步骤 6：创建测试程序和数据
- 创建 `test/` 目录
- 编写 6 个测试程序
- 创建测试数据文件（content.md、content.html）
- 复制模板到 `test/data/template.docx`

### 步骤 7：更新 `CMakeLists.txt`
- 更新源文件列表
- 添加测试目标

### 步骤 8：编译并运行所有测试
- `cmake .. && make -j$(nproc)`
- 逐个运行测试程序，验证 PASS

---

## 验证方法

1. **编译验证**：`cmake .. && make -j$(nproc)` 无错误
2. **CLI 验证**：
   - `./test_docx ./联合纪要说明.docx "{{*}}" "小红有才"` → 8 处替换
   - `./test_docx ./联合纪要说明.docx --md "正文" "## 标题\n正文"` → 1 处替换
   - `./test_docx ./联合纪要说明.docx --html "正文" "<h2>标题</h2><p>正文</p>"` → 1 处替换
3. **测试程序验证**：6 个测试程序全部输出 PASS
4. **流式模式验证**：`test_streaming` 确认 `isStreamingMode() == true` 且替换结果正确
5. **格式保留验证**：解压输出 docx，检查 `document.xml` 中字体（方正小标宋、仿宋等）和 `<w:rPr>` 标记保留

---

## 假设与决策

1. **完全替换 API**：用户确认删除自由函数，只保留 class API
2. **测试程序独立**：每个测试场景编译为独立可执行文件
3. **测试数据在 `test/data/`**：提供 md/html 测试输入文件
4. **流式判断依据**：`fileSize + replacementContentSize > memoryLimit`
5. **流式粒度**：段落级流式（按 `<w:p>` 边界分块），内存 O(最大段落大小)
6. **流式模式限制**：流式模式下多次 `replaceText` 调用会多次扫描文件（不缓存 DOM），但内存占用恒定
7. **DOM 模式缓存**：首次 `replaceText` 加载 DOM 后，后续调用复用（支持多次替换不重新解压）
8. **`rich_content.h` 保留自由函数**：`parseMarkdown`/`parseHtml` 保持为自由函数，因为它们是无状态工具函数
9. **`zip_utils.h`/`xml_utils.h` 不变**：内部工具，保持现有接口
