# docx_temp_helper

DOCX 模板占位符替换与文档生成库 —— 面向边缘设备的轻量 C++17 解决方案。

---

## 目录

- [1. 功能概述](#1-功能概述)
- [2. 项目结构设计](#2-项目结构设计)
- [3. 编译要求与构建](#3-编译要求与构建)
- [4. 架构设计与实现](#4-架构设计与实现)
- [5. API 接口文档](#5-api-接口文档)
- [6. 使用示例](#6-使用示例)
- [7. 命令行工具](#7-命令行工具)
- [8. 测试程序](#8-测试程序)
- [9. 格式标准](#9-格式标准)
- [10. 错误码说明](#10-错误码说明)
- [11. 限制与注意事项](#11-限制与注意事项)

---

## 1. 功能概述

本库提供两类核心功能：

| 功能 | 说明 |
|------|------|
| **模板占位符替换** | 将 `.docx` 模板中的 `{{xx}}` 占位符替换为文本或富文本内容，保留原有排版格式 |
| **空白模板文档生成** | 基于空白模板，传入标题+正文（纯文本/Markdown/HTML），生成完整文档 |

核心特性：
- 面向对象设计：`DocxDocument` 类管理文档生命周期（open → 替换/生成 → save → close）
- 跨 Run 占位符替换：Word 将 `{{` `名称` `}}` 拆分到不同 `<w:r>` 中也能正确处理
- 格式保留：纯文本替换仅修改 `<w:t>` 文本，不改变 `<w:rPr>` 字体/字号/样式
- 富文本渲染：HTML/Markdown → GB/T 9704-2012 公文格式段落
- 双处理模式：DOM 模式（常规）与流式模式（大文件），根据内存限制自动选择
- 免费闭源商用：minizip (zlib License) + pugixml (MIT License)

---

## 2. 项目结构设计

```
docx_temp_helper/
├── CMakeLists.txt                              # CMake 构建配置（C++17）
├── README.md                                   # 本文档
│
├── include/docx_temp_helper/                   # ── 公开头文件（对外 API）──
│   ├── docx_document.h                         #   DocxDocument 类 + 错误/配置类型
│   └── rich_content.h                          #   富文本模块：ContentType / RichParagraph / 解析渲染函数
│
├── src/                                        # ── 内部实现（不对外暴露）──
│   ├── docx_document.cpp                       #   主实现：占位符替换 + 文档生成（DOM/流式双模式）
│   ├── streaming_processor.h                   #   流式 XML 处理器（内部头文件）
│   ├── streaming_processor.cpp                 #   流式处理实现：64KB 块读取，按 <w:p> 段落边界处理
│   ├── rich_content.cpp                        #   MD/HTML 解析 + OOXML 段落渲染
│   ├── zip_utils.h / zip_utils.cpp             #   ZIP 工具（minizip 封装：解压/压缩）
│   └── xml_utils.h / xml_utils.cpp             #   XML 工具（pugixml 封装：run 收集/文本设置）
│
├── test/                                       # ── 测试程序 ──
│   ├── test_plain.cpp                          #   测试1：纯文本批量替换
│   ├── test_markdown.cpp                       #   测试2：Markdown 富文本替换
│   ├── test_html.cpp                           #   测试3：HTML 富文本替换
│   ├── test_mixed.cpp                          #   测试4：混合替换（纯文本 + Markdown）
│   ├── test_streaming.cpp                      #   测试5：流式处理模式
│   ├── test_error.cpp                          #   测试6：错误处理
│   ├── test_generate.cpp                       #   测试7：从空白模板生成文档
│   ├── test_zip.cpp                            #   测试8：ZIP 工具函数
│   └── data/                                   #   测试数据
│       ├── template.docx                       #     占位符测试模板
│       ├── content.md                          #     Markdown 测试内容（~9000字）
│       └── content.html                        #     HTML 测试内容（~9000字）
│
├── third_party/                                # ── 三方库（从源码编译）──
│   ├── minizip/
│   │   ├── build.sh                            #   拉取 zlib v1.3.1 + minizip 源码并编译
│   │   ├── include/                            #   zip.h, unzip.h, ioapi.h, crypt.h
│   │   └── lib/                                #   libminizip.a
│   └── pugixml/
│       ├── build.sh                            #   拉取 pugixml 源码并编译
│       ├── include/                            #   pugixml.hpp, pugiconfig.hpp
│       └── lib/                                #   libpugixml.a
│
├── templates/                                  # ── 模板文件 ──
│   └── 默认模板.docx                            #   空白模板（用于 generateDocument）
│
└── main.cpp                                    # test_docx CLI 工具
```

**分层设计**：

| 层次 | 文件 | 职责 |
|------|------|------|
| **公开 API 层** | `docx_document.h`, `rich_content.h` | 对外接口声明，调用方只需包含这两个头文件 |
| **业务实现层** | `docx_document.cpp`, `rich_content.cpp` | 占位符查找/替换、文档生成、富文本解析渲染 |
| **流式处理层** | `streaming_processor.h/.cpp` | 大文件按段落逐段处理，内存占用 O(最大段落) |
| **工具层** | `zip_utils.*`, `xml_utils.*` | minizip/pugixml 封装，提供底层 I/O 能力 |

---

## 3. 编译要求与构建

### 3.1 编译环境要求

| 依赖 | 最低版本 | 说明 |
|------|---------|------|
| C++ 编译器 | g++ >= 7 或 clang++ >= 5 | 需支持 C++17 |
| CMake | >= 3.10 | 构建系统 |
| zlib 开发库 | >= 1.2.11 | minizip 依赖，`sudo apt install zlib1g-dev` |
| make | 任意 | 构建工具 |
| 网络访问 | - | 首次构建需从 GitHub/Gitee 拉取三方库源码 |

### 3.2 编译步骤

```bash
# 第1步：编译三方库（仅首次构建需要）
cd third_party/pugixml && ./build.sh
cd ../minizip && ./build.sh

# 第2步：编译主项目
cd ../../build
cmake ..
make -j$(nproc)

# 第3步：运行测试
ctest --output-on-failure

# 第4步：运行 CLI 工具
./test_docx ./templates/联合纪要说明.docx "{{*}}" "小红有才"
```

### 3.3 三方库 build.sh 说明

`build.sh` 脚本自动完成：
1. 从 GitHub 拉取源码（失败自动回退 Gitee 镜像）
2. 编译为静态库（`.a`），添加 `-fPIE` 以支持 PIE 链接
3. 安装头文件到 `include/`，库文件到 `lib/`
4. 清理中间产物

minizip 使用 zlib v1.3.1 稳定版的 `contrib/minizip`，避免 master 分支的额外依赖。

### 3.4 交叉编译

边缘设备如需交叉编译，修改 `build.sh` 中的 `CC`/`CXX`/`AR` 环境变量即可：

```bash
CC=aarch64-linux-gnu-gcc CXX=aarch64-linux-gnu-g++ ./build.sh
```

CMake 交叉编译时需指定工具链文件：

```bash
cmake .. -DCMAKE_TOOLCHAIN_FILE=toolchain-aarch64.cmake
```

---

## 4. 架构设计与实现

### 4.1 核心类：DocxDocument

```cpp
class DocxDocument {
public:
    // 生命周期
    DocxDocument();
    explicit DocxDocument(const DocxConfig& config);
    ~DocxDocument();  // 析构自动清理临时目录

    ErrorInfo open(const std::string& path);   // 解压 docx 到临时目录
    ErrorInfo save(const std::string& path);   // 重新压缩为 docx
    void close();                               // 清理临时目录

    // 文本替换
    ReplaceResult replaceText(const std::map<std::string, std::string>& replacements);
    ReplaceResult replaceText(const std::string& pattern, const std::string& replacement);

    // 富文本替换
    ReplaceResult replaceRich(const std::map<std::string, RichReplacement>& replacements);

    // 文档生成（从空白模板）
    ReplaceResult generateDocument(const std::string& title,
                                    const std::string& bodyContent,
                                    ContentType contentType);

    // 状态查询
    bool isOpen() const;
    bool isStreamingMode() const;
    size_t getFileSize() const;
    const DocxConfig& config() const;
};
```

**设计要点**：
- **open → 操作 → save → close** 生命周期模型，打开一次可多次操作后一次保存
- **RAII 资源管理**：析构函数自动调用 `close()` 清理临时目录
- **禁拷贝允移动**：持有临时目录等资源不可拷贝，但可移动转移所有权
- **配置集中**：`DocxConfig` 统一管理内存限制、临时目录、日志等参数

### 4.2 双处理模式

内部根据 `文件大小 + 替换内容大小` 与 `memoryLimit` 比较，自动选择处理模式：

| 模式 | 触发条件 | 内存占用 | 实现方式 |
|------|---------|---------|---------|
| **DOM 模式** | 总大小 ≤ `memoryLimit` | O(文件大小) | pugixml 全量加载 `document.xml` 到内存，支持多次替换复用 DOM |
| **流式模式** | 总大小 > `memoryLimit` | O(最大段落大小) | 64KB 块读取，按 `<w:p>` 边界逐段处理，段落外内容直接透传 |

两种模式均支持表格单元格内的占位符替换（流式模式扫描所有 `<w:p>` 标签，不区分位置）。

```
用户调用 replaceText/replaceRich
         │
         ▼
  shouldUseStreaming_() 判断
         │
    ┌────┴────┐
    ▼         ▼
 DOM 模式   流式模式
    │         │
    ▼         ▼
 parseXmlDom_()  streamProcessXml()
 全量加载 DOM     按段落回调处理
    │         │
    ▼         ▼
 replaceTextDom_()  replaceTextStreaming_()
 遍历 <w:p> 逐段替换   逐段读取+替换+写入
    │         │
    └────┬────┘
         ▼
   saveXmlDom_() / 流式输出
         │
         ▼
     zip_() 压缩
```

### 4.3 占位符替换实现

**跨 Run 问题**：Word 编辑器常将 `{{会议主题}}` 拆分到多个 `<w:r>` 中：

```xml
<w:p>
  <w:r><w:rPr>仿宋16pt</w:rPr><w:t>{{</w:t></w:r>
  <w:r><w:rPr>仿宋16pt</w:rPr><w:t>会议主题</w:t></w:r>
  <w:r><w:rPr>仿宋16pt</w:rPr><w:t>}}</w:t></w:r>
</w:p>
```

**解决方案：逐字符法**：
1. 将段落内所有 `<w:r>` 的文本拼接为完整字符串
2. 在完整字符串中查找 `{{key}}` 匹配，记录匹配区间 `[startPos, endPos)`
3. 逐字符遍历，保留匹配区间外的字符，替换区间内字符为替换文本
4. 将结果写回各 `<w:r>` 的 `<w:t>` 节点，保留原有 `<w:rPr>` 格式

### 4.4 富文本渲染实现

HTML/Markdown 替换流程：

```
输入字符串 (HTML/Markdown)
         │
         ▼
  parseHtml() / parseMarkdown()
         │
         ▼
  vector<RichParagraph>    ← 结构化段落（标题级别、runs、对齐、缩进）
         │
         ▼
  createParagraphNode()    ← 渲染为 OOXML <w:p> 元素
         │
         ▼
  替换占位符所在 <w:p>      ← 删除原段落，插入新段落
```

**渲染规则**（GB/T 9704-2012）：
- 标题 h1-h4 分别使用方正小标宋简体/黑体/楷体/仿宋
- 正文使用仿宋 16pt，首行缩进 2 字符（640 twips），两端对齐
- 列表使用仿宋 16pt，悬挂缩进
- 西文字符统一使用 Times New Roman

### 4.5 文档生成实现

`generateDocument` 从空白模板生成文档：
1. 打开空白模板（`<w:body>` 中仅有 `<w:sectPr>`，无段落）
2. 若提供标题，创建 h1 段落（方正小标宋简体 22pt 居中）
3. 根据内容类型解析正文：
   - Plain：按 `\n` 分割，每行生成仿宋正文段落
   - HTML/Markdown：复用富文本解析器生成格式化段落
4. 将所有段落插入到 `<w:sectPr>` 之前

---

## 5. API 接口文档

### 5.1 配置：DocxConfig

```cpp
struct DocxConfig {
    size_t memoryLimit = 10 * 1024 * 1024;  // 内存限制（字节），默认 10MB
    std::string tempDir;                     // 临时解压目录，空则使用系统临时目录
    bool verbose = false;                    // 是否输出详细日志
    bool keepTempDir = false;                // 是否保留临时目录（调试用）
};
```

### 5.2 生命周期管理

```cpp
ErrorInfo open(const std::string& path);    // 打开 docx，解压到临时目录
ErrorInfo save(const std::string& path);    // 保存为 docx
void close();                                // 关闭并清理临时目录
```

### 5.3 文本替换：replaceText

```cpp
// 批量替换：传入 map<string,string>
ReplaceResult replaceText(const std::map<std::string, std::string>& replacements);

// 单模式替换
ReplaceResult replaceText(const std::string& pattern, const std::string& replacement);
```

| 参数 | 说明 |
|------|------|
| `replacements` | key 为占位符名称（不含 `{{}}`），value 为替换文本；key 为 `"*"` 通配所有 |
| `pattern` | `"{{*}}"` 匹配所有占位符；`"{{会议主题}}"` 精确匹配 |
| `replacement` | 替换文本 |

**行为**：遍历所有 `<w:p>` 段落（含表格单元格），查找 `{{key}}` 替换为 value，保留原有 `<w:rPr>` 格式。

### 5.4 富文本替换：replaceRich

```cpp
ReplaceResult replaceRich(const std::map<std::string, RichReplacement>& replacements);
```

```cpp
struct RichReplacement {
    std::string content;
    ContentType type = ContentType::Plain;
};
```

| ContentType | 处理方式 | 替换粒度 |
|-------------|---------|---------|
| `Plain` | 走文本替换逻辑 | 文本级（修改 `<w:t>`） |
| `HTML` | 解析 HTML 生成格式化段落 | 段落级（替换整个 `<w:p>`） |
| `Markdown` | 解析 MD 生成格式化段落 | 段落级（替换整个 `<w:p>`） |

**HTML/Markdown 规则**：仅替换**独占一行**的占位符（段落全文只有 `{{xx}}`），删除原段落并插入新段落。

### 5.5 文档生成：generateDocument

```cpp
ReplaceResult generateDocument(const std::string& title,
                                const std::string& bodyContent,
                                ContentType contentType);
```

| 参数 | 说明 |
|------|------|
| `title` | 标题（可选，为空则不添加标题）。以 h1 格式渲染：方正小标宋简体 22pt 居中 |
| `bodyContent` | 正文内容 |
| `contentType` | 内容类型：`Plain` / `HTML` / `Markdown` |

**行为**：在空白模板的 `<w:body>` 中插入标题和正文段落。需先调用 `open()` 打开空白模板。

### 5.6 状态查询

```cpp
bool isOpen() const;           // 文档是否已打开
bool isStreamingMode() const;  // 是否处于流式处理模式
size_t getFileSize() const;    // 获取已打开文件大小（字节）
const DocxConfig& config() const;  // 获取配置
```

### 5.7 富文本模块独立使用

```cpp
#include "docx_temp_helper/rich_content.h"

// 解析 Markdown/HTML 为结构化段落
auto paragraphs = docx_temp_helper::parseMarkdown("# 标题\n正文");
auto paragraphs = docx_temp_helper::parseHtml("<h2>标题</h2><p>正文</p>");

// 渲染到已有 XML 文档（DOM 模式）
docx_temp_helper::renderParagraphsToXml(parentNode, paragraphs, placeholderNode);

// 序列化为 XML 字符串（流式模式）
std::string xml = docx_temp_helper::serializeParagraphs(paragraphs);

// 插入到指定节点之前（文档生成）
docx_temp_helper::appendParagraphsBefore(parentNode, paragraphs, sectPrNode);
```

### 5.8 ZIP 工具函数

独立的 ZIP 压缩/解压函数，返回 `ErrorInfo` 复用错误码体系，不依赖 `DocxDocument`：

```cpp
#include "docx_temp_helper/docx_document.h"

// 压缩：目录或文件 → ZIP
ErrorInfo zipCompress(const std::string& srcPath,
                       const std::string& outputPath,
                       const std::string& zipName);

// 解压：ZIP → 目录
ErrorInfo zipExtract(const std::string& zipPath,
                      const std::string& destDir);
```

| 函数 | 参数 | 说明 |
|------|------|------|
| `zipCompress` | `srcPath` | 源路径（目录或文件均可） |
| | `outputPath` | 输出目录路径（不存在则自动创建） |
| | `zipName` | ZIP 包名（如 `"archive.zip"`） |
| `zipExtract` | `zipPath` | ZIP 文件路径 |
| | `destDir` | 解压目标目录（不存在则自动创建） |

**行为**：
- `srcPath` 为目录时，递归打包其下所有文件，保持目录结构
- `srcPath` 为文件时，打包单个文件（ZIP 内仅含文件名）
- 输出的 ZIP 使用正斜杠 `/` 路径分隔符，Windows/Mac/Linux 均可原生解压
- 使用 `Z_DEFLATED` 标准压缩算法，与系统自带工具完全兼容

---

## 6. 使用示例

### 6.1 纯文本批量替换

```cpp
#include "docx_temp_helper/docx_document.h"

int main() {
    std::map<std::string, std::string> replacements = {
        {"会议主题", "年终总结会议"},
        {"会议时间", "2024年12月25日"},
        {"主持人", "张三"},
    };

    docx_temp_helper::DocxDocument doc;
    if (!doc.open("template.docx").ok()) return 1;

    auto result = doc.replaceText(replacements);
    if (result.ok()) {
        printf("替换 %d 处\n", result.totalReplaced);
        doc.save("output.docx");
    }

    doc.close();
    return result.ok() ? 0 : 1;
}
```

### 6.2 通配符替换

```cpp
docx_temp_helper::DocxDocument doc;
doc.open("template.docx");

// 所有 {{xx}} 替换为同一文本
doc.replaceText("{{*}}", "小红有才");
doc.save("output.docx");
doc.close();
```

### 6.3 富文本混合替换

```cpp
#include "docx_temp_helper/docx_document.h"
#include "docx_temp_helper/rich_content.h"

int main() {
    std::map<std::string, docx_temp_helper::RichReplacement> replacements;

    // 纯文本替换（保留原格式）
    replacements["标题"] = {"2024年度工作报告", docx_temp_helper::ContentType::Plain};

    // Markdown 替换（生成格式化段落）
    replacements["正文"] = {
        "## 一、工作总结\n\n"
        "本年度完成主要工作：\n\n"
        "1. 系统架构升级\n"
        "2. 性能优化\n\n"
        "## 二、下年度计划\n\n"
        "继续推进**数字化转型**。",
        docx_temp_helper::ContentType::Markdown
    };

    // HTML 替换
    replacements["备注"] = {
        "<h3>注意事项</h3>"
        "<p>请各部门提前准备<b>汇报材料</b>。</p>"
        "<ul><li>材料需含<i>数据图表</i></li></ul>",
        docx_temp_helper::ContentType::HTML
    };

    docx_temp_helper::DocxDocument doc;
    doc.open("template.docx");
    auto result = doc.replaceRich(replacements);
    if (result.ok()) doc.save("output.docx");
    doc.close();
    return result.ok() ? 0 : 1;
}
```

### 6.4 从空白模板生成文档

```cpp
#include "docx_temp_helper/docx_document.h"
#include "docx_temp_helper/rich_content.h"

int main() {
    docx_temp_helper::DocxDocument doc;
    doc.open("templates/默认模板.docx");

    // 传入标题 + Markdown 正文
    std::string mdContent = "## 会议内容\n\n"
                            "本次讨论了以下事项：\n\n"
                            "- 项目进度\n"
                            "- 预算审批\n";

    doc.generateDocument("季度工作总结会议纪要", mdContent,
                          docx_temp_helper::ContentType::Markdown);
    doc.save("output.docx");
    doc.close();
    return 0;
}
```

### 6.5 自定义配置

```cpp
docx_temp_helper::DocxConfig config;
config.tempDir = "/my/custom/temp";    // 自定义临时目录（SSD 加速）
config.keepTempDir = true;              // 保留临时文件用于调试
config.verbose = true;                  // 输出详细日志
config.memoryLimit = 5 * 1024 * 1024;  // 5MB 内存限制

docx_temp_helper::DocxDocument doc(config);
doc.open("input.docx");
doc.replaceText("{{*}}", "替换文本");
doc.save("output.docx");
doc.close();

// 调试：检查 /my/custom/temp/word/document.xml
```

### 6.6 错误处理

```cpp
docx_temp_helper::DocxDocument doc;

auto openErr = doc.open("input.docx");
if (!openErr.ok()) {
    switch (openErr.code) {
        case docx_temp_helper::ErrorCode::FileNotFound:
            std::cerr << "文件不存在: " << openErr.detail << std::endl;
            break;
        case docx_temp_helper::ErrorCode::UnzipFailed:
            std::cerr << "文件可能损坏" << std::endl;
            break;
        default:
            std::cerr << openErr.toString() << std::endl;
    }
    return 1;
}

auto result = doc.replaceText("{{*}}", "替换文本");
if (!result.ok()) {
    std::cerr << result.error.toString() << std::endl;
    doc.close();
    return 1;
}

doc.save("output.docx");
doc.close();
```

### 6.7 ZIP 工具：压缩/解压

```cpp
#include "docx_temp_helper/docx_document.h"

// 压缩目录
auto err = docx_temp_helper::zipCompress(
    "/home/user/documents",    // 源目录
    "/home/user/output",       // 输出目录（自动创建）
    "backup.zip");              // 包名
if (!err.ok()) {
    std::cerr << err.toString() << std::endl;
}

// 压缩单个文件
auto err2 = docx_temp_helper::zipCompress(
    "/home/user/report.pdf",   // 源文件
    "/home/user/output",       // 输出目录
    "report.zip");              // 包名

// 解压
auto err3 = docx_temp_helper::zipExtract(
    "/home/user/output/backup.zip",  // ZIP 文件
    "/home/user/restored");           // 解压目录（自动创建）
if (!err3.ok()) {
    std::cerr << err3.toString() << std::endl;
}
```

---

## 7. 命令行工具

编译后生成 `test_docx` CLI 工具：

```bash
# 纯文本替换
./test_docx ./template.docx "{{*}}" "小红有才"

# Markdown 替换
./test_docx ./template.docx --md "正文" "## 会议内容

本次会议讨论了以下事项：

- 项目进度汇报
- 预算审批

**重要决定**：项目延期。"

# HTML 替换
./test_docx ./template.docx --html "正文" "<h2>会议内容</h2><p>讨论了<b>项目进度</b></p>"
```

---

## 8. 测试程序

8 个独立测试程序，覆盖所有功能：

| 测试程序 | 源文件 | 测试内容 | 断言数 |
|---------|--------|---------|--------|
| `test_plain` | `test/test_plain.cpp` | 纯文本批量替换 + 通配符替换 | 11 |
| `test_markdown` | `test/test_markdown.cpp` | Markdown 富文本替换（标题/列表/加粗/斜体） | 10 |
| `test_html` | `test/test_html.cpp` | HTML 富文本替换（标签解析和格式渲染） | 10 |
| `test_mixed` | `test/test_mixed.cpp` | 混合替换（纯文本 + Markdown） | 10 |
| `test_streaming` | `test/test_streaming.cpp` | 流式处理模式（1KB 内存限制强制启用） | 13 |
| `test_error` | `test/test_error.cpp` | 错误处理（文件不存在/空映射/未打开等） | 6 |
| `test_generate` | `test/test_generate.cpp` | 从空白模板生成文档（MD/HTML/Plain/无标题/错误） | 28 |
| `test_zip` | `test/test_zip.cpp` | ZIP 工具函数（目录/文件压缩、解压、docx 兼容、错误处理） | 15 |

**测试数据**：
- `test/data/template.docx` - 包含多个占位符的测试模板
- `test/data/content.md` - Markdown 测试内容（~9000字，13个章节）
- `test/data/content.html` - HTML 测试内容（~9000字，13个章节）
- `templates/默认模板.docx` - 空白模板（用于 generateDocument 测试）

**运行测试**：

```bash
cd build && cmake .. && make -j$(nproc) && ctest --output-on-failure
```

测试通过后保留输出 `.docx` 文件（位于 `test/data/` 目录），供人工检查：

```
output_plain.docx              # 纯文本替换结果
output_plain_wildcard.docx     # 通配符替换结果
output_md.docx                 # Markdown 替换结果
output_html.docx               # HTML 替换结果
output_mixed.docx              # 混合替换结果
output_streaming.docx          # 流式纯文本替换结果
output_streaming_md.docx       # 流式 Markdown 替换结果
output_generate_md.docx        # 空白模板生成（Markdown）
output_generate_html.docx      # 空白模板生成（HTML）
output_generate_plain.docx     # 空白模板生成（纯文本）
output_generate_notitle.docx   # 空白模板生成（无标题）
```

---

## 9. 格式标准

### GB/T 9704-2012 公文格式映射

| 元素 | 字体 | 字号 | 对齐 | 缩进 | 行距 |
|------|------|------|------|------|------|
| h1 | 方正小标宋简体 | 22pt | 居中 | 无 | 1.0 |
| h2 | 黑体 | 16pt | 左对齐 | 无 | 1.0 |
| h3 | 楷体 | 16pt | 左对齐 | 无 | 1.0 |
| h4 / 正文 | 仿宋 | 16pt | 两端对齐 | 首行缩进 32pt | 1.0 |
| 列表 | 仿宋 | 16pt | 左对齐 | 悬挂缩进 | 1.0 |
| 西文 | Times New Roman | 同字号 | - | - | - |

### OOXML 单位换算

| 单位 | 换算 | 示例 |
|------|------|------|
| 字体大小 | 1pt = 2 half-points | 16pt → `<w:sz w:val="32"/>` |
| 缩进 | 1pt = 20 twips | 32pt → `<w:ind w:firstLine="640"/>` |
| 行距 | 240 = 1.0 倍 | 1.0 → `<w:spacing w:line="240"/>` |

### 支持的 Markdown 语法

| 语法 | 示例 | 渲染效果 |
|------|------|---------|
| 标题 | `# h1` ~ `#### h4` | h1~h4 |
| 加粗 | `**text**` | **加粗** |
| 斜体 | `*text*` | *斜体* |
| 无序列表 | `- item` 或 `* item` | 列表项 |
| 有序列表 | `1. item` | 有序列表 |
| 段落 | 空行分隔 | 正文段落 |

### 支持的 HTML 标签

| 标签 | 渲染效果 |
|------|---------|
| `<h1>` ~ `<h4>` | h1~h4 |
| `<p>` | 正文段落 |
| `<b>` / `<strong>` | 加粗 |
| `<i>` / `<em>` | 斜体 |
| `<u>` | 下划线 |
| `<ul>` / `<ol>` / `<li>` | 无序/有序列表 |
| `<br>` | 换行 |

---

## 10. 错误码说明

| 错误码 | 值 | 说明 | 常见原因 |
|--------|---|------|---------|
| `Ok` | 0 | 成功 | - |
| `FileNotFound` | 1 | 输入文件不存在 | 路径错误 |
| `FileNotReadable` | 2 | 文件不可读 | 权限不足 |
| `InvalidDocxFormat` | 3 | 非合法 docx | 文件损坏 |
| `UnzipFailed` | 4 | 解压失败 | zip 结构损坏 |
| `XmlParseFailed` | 5 | XML 解析失败 | document.xml 格式错误 |
| `XmlSaveFailed` | 6 | XML 保存失败 | 磁盘空间不足 |
| `ZipFailed` | 7 | 压缩失败 | 磁盘空间不足 |
| `TempDirCreateFailed` | 8 | 临时目录创建失败 | `/tmp` 不可写 |
| `NoMatchFound` | 9 | 未找到匹配的占位符 | 模板无对应 `{{xx}}` |
| `InvalidPattern` | 10 | 占位符模式非法 | 替换映射为空 / 正文为空 |
| `OutputWriteFailed` | 11 | 输出文件写入失败 | 路径不可写 |
| `NotOpened` | 12 | 文档未打开 | 未调用 `open()` 或已 `close()` |
| `UnknownError` | 13 | 未知错误 | - |

```cpp
struct ErrorInfo {
    ErrorCode code = ErrorCode::Ok;
    std::string message;   // 人可读的错误描述
    std::string detail;    // 附加上下文（文件路径等）
    bool ok() const;
    std::string toString() const;
};

struct ReplaceResult {
    ErrorInfo error;
    std::vector<ReplaceRecord> records;  // 逐项替换记录
    int totalReplaced = 0;
    bool ok() const;
};

struct ReplaceRecord {
    std::string placeholder;  // 如 "{{会议主题}}"
    std::string replacement;
    int count = 0;            // 替换次数
};
```

---

## 11. 限制与注意事项

1. **仅处理 `word/document.xml`**：不处理页眉/页脚/尾注/批注中的占位符
2. **富文本仅替换独占段落**：HTML/Markdown 替换只对占位符独占一行的段落生效（段落全文仅 `{{xx}}`）
3. **不支持嵌套占位符**：`{{outer{{inner}}content}}` 无法正确处理
4. **不支持图片/表格**：富文本不支持插入图片或表格，仅支持段落级别内容
5. **字形依赖**：GB/T 9704-2012 标准字体（方正小标宋简体、黑体、楷体、仿宋）需在目标系统安装
6. **generateDocument 始终使用 DOM 模式**：空白模板很小，无需流式处理
7. **ZIP 跨平台兼容**：`zipCompress` 生成的 ZIP 文件使用正斜杠路径分隔符和 `Z_DEFLATED` 标准压缩，Windows/Mac/Linux 系统自带工具均可直接解压
