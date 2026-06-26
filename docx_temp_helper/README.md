# docx_temp_helper

DOCX 模板占位符替换库 —— 面向边缘设备的轻量 C++17 解决方案。

---

## 目录

- [1. 概述](#1-概述)
- [2. 三方库选择](#2-三方库选择)
- [3. 目录结构](#3-目录结构)
- [4. 构建方式](#4-构建方式)
- [5. 架构设计](#5-架构设计)
- [6. 性能分析](#6-性能分析)
- [7. API 接口文档](#7-api-接口文档)
- [8. 错误码说明](#8-错误码说明)
- [9. 使用示例](#9-使用示例)
- [10. 测试程序](#10-测试程序)
- [11. 格式标准](#11-格式标准)
- [12. 限制与注意事项](#12-限制与注意事项)

---

## 1. 概述

本库提供对 `.docx` 文件中 `{{xx}}` 占位符的替换功能：

- **纯文本替换**：将 `{{会议主题}}` 替换为指定文本，保留原有字体/字号/段落格式
- **富文本替换**：将 `{{正文}}` 替换为 Markdown/HTML 格式化内容，生成符合公文标准的段落

核心特性：
- 面向对象设计：`DocxDocument` 类管理文档生命周期，支持打开→多次替换→保存
- 跨 Run 占位符替换（Word 将 `{{` `名称` `}}` 拆分到不同 `<w:r>` 中也能正确处理）
- 保留 OOXML 中所有 `<w:rPr>` 格式属性
- 支持 GB/T 9704-2012 公文格式标准
- 流式处理模式：当文件过大时自动启用，内存占用 O(最大段落大小)
- 内存限制可配置（默认 10MB）
- 详细的错误码与错误信息
- 临时目录可控（支持调试与嵌入式场景）
- 三方库从源码编译，允许免费闭源商用

---

## 2. 三方库选择

| 库 | 用途 | 许可证 | 选择理由 |
|------|------|--------|----------|
| **minizip** | ZIP 读写（解压/压缩 docx） | zlib License | 轻量零依赖，允许免费闭源商用；zlib License 无 copyleft 限制 |
| **pugixml** | XML 解析与修改 | MIT License | 高性能 DOM 解析（比 tinyxml 快 5-10 倍），支持修改后保存；MIT 允许闭源商用 |

### 为什么不用其他库？

| 候选 | 排除原因 |
|------|----------|
| libzip | 依赖 zlib + 可选加密库，编译复杂度高 |
| zlib/contrib/minizip-ng | 额外依赖（取巧但引入更多文件） |
| tinyxml2 | 不支持修改后保存为格式化 XML |
| rapidxml | 不再维护，C++11 兼容性差 |
| RapidJSON | JSON 库，不适用于 OOXML |

---

## 3. 目录结构

```
docx_temp_helper/
├── CMakeLists.txt                              # CMake 构建配置（C++17）
├── README.md                                   # 本文档
│
├── include/docx_temp_helper/
│   ├── docx_document.h                         # 公开 API：DocxDocument 类 + 错误/配置类型
│   └── rich_content.h                          # 富文本模块：ContentType / RichParagraph / 解析渲染函数
│
├── src/
│   ├── docx_document.cpp                       # 主实现：占位符查找 + 逐字符替换（DOM/流式双模式）
│   ├── streaming_processor.h                   # 流式 XML 处理器（内部头文件，不对外暴露）
│   ├── streaming_processor.cpp                 # 流式处理实现：按段落边界逐段处理
│   ├── rich_content.cpp                        # MD/HTML 解析 + OOXML 段落渲染
│   ├── zip_utils.h / zip_utils.cpp             # ZIP 工具（minizip 封装）
│   └── xml_utils.h / xml_utils.cpp             # XML 工具（pugixml 封装）
│
├── test/
│   ├── test_plain.cpp                          # 测试：纯文本批量替换
│   ├── test_markdown.cpp                       # 测试：Markdown 富文本替换
│   ├── test_html.cpp                           # 测试：HTML 富文本替换
│   ├── test_mixed.cpp                          # 测试：混合替换（纯文本 + 富文本）
│   ├── test_streaming.cpp                      # 测试：流式处理模式（低内存限制）
│   ├── test_error.cpp                          # 测试：错误处理
│   └── data/
│       ├── template.docx                       # 测试模板
│       ├── content.md                          # Markdown 测试内容
│       └── content.html                        # HTML 测试内容
│
├── third_party/
│   ├── minizip/
│   │   ├── build.sh                            # 拉取源码并编译（GitHub → Gitee 回退）
│   │   ├── include/                            # zip.h, unzip.h, ioapi.h, crypt.h
│   │   └── lib/                                # libminizip.a
│   └── pugixml/
│       ├── build.sh                            # 拉取源码并编译
│       ├── include/                            # pugixml.hpp, pugiconfig.hpp
│       └── lib/                                # libpugixml.a
│
├── templates/
│   └── summary/                                # 测试用模板文件
│
└── main.cpp                                    # test_docx CLI 工具
```

---

## 4. 构建方式

### 4.1 依赖

- C++17 编译器（g++ >= 7 或 clang++ >= 5）
- CMake >= 3.10
- zlib 开发库（`sudo apt install zlib1g-dev`）

### 4.2 编译步骤

```bash
# 第1步：编译三方库（首次构建时执行）
cd third_party/pugixml && ./build.sh
cd ../minizip && ./build.sh

# 第2步：编译主项目
cd ../../build
cmake ..
make -j$(nproc)

# 第3步：运行 CLI 工具
./test_docx ./联合纪要说明.docx "{{*}}" "小红有才"

# 第4步：运行所有测试
ctest
```

### 4.3 build.sh 说明

三方库的 `build.sh` 脚本会：
1. 从 GitHub 拉取源码（失败自动回退 Gitee 镜像）
2. 编译为静态库（`.a`）
3. 安装头文件到 `include/`，库文件到 `lib/`
4. 清理中间产物

minizip 使用稳定版 zlib v1.3.1 的 `contrib/minizip`，避免 master 分支的额外依赖。

---

## 5. 架构设计

### 5.1 面向对象设计

本库采用 **class 设计**，核心类是 `DocxDocument`：

```cpp
class DocxDocument {
public:
    // 生命周期管理
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
};
```

### 5.2 为什么选择 class？

| 考量维度 | 说明 |
|---------|------|
| **多次操作同一文档** | 打开一次后可多次调用 `replaceText`/`replaceRich`，最后一次性保存，避免重复解压/压缩 |
| **状态管理** | 对象持有 DOM 文档、临时目录等状态，方法间自然共享 |
| **资源管理** | RAII：析构函数自动清理临时目录，避免资源泄漏 |
| **流式模式** | 根据文件大小自动选择 DOM 或流式处理，对调用方透明 |
| **配置集中** | `DocxConfig` 统一管理内存限制、临时目录、日志开关等参数 |

### 5.3 双处理模式

内部根据文件大小自动选择处理模式：

| 模式 | 触发条件 | 内存占用 | 适用场景 |
|------|---------|---------|---------|
| **DOM 模式** | 文件大小 + 替换内容 ≤ `memoryLimit` | O(文件大小) | 常规文档 |
| **流式模式** | 文件大小 + 替换内容 > `memoryLimit` | O(最大段落大小) | 大文件、内存受限设备 |

流式模式采用 64KB 块读取，按 `<w:p>` 段落边界逐段处理，段落外内容直接透传。

---

## 6. 性能分析

### 6.1 实际性能瓶颈

| 阶段 | 耗时占比 | 说明 |
|------|---------|------|
| 解压 docx | ~30% | minizip 解压 ZIP，I/O 密集 |
| XML 解析 | ~15% | pugixml DOM 解析 word/document.xml |
| 占位符查找+替换 | ~10% | 逐字符遍历 runs，O(n) 线性扫描 |
| XML 保存 | ~10% | pugixml 序列化回 XML |
| 压缩 docx | ~35% | minizip 压缩 ZIP，CPU 密集 |

### 6.2 优化建议

- **批量替换**：使用 `map<string,string>` 一次传入所有替换项，避免多次调用
- **临时目录**：`config.tempDir` 指定 SSD 路径，减少 I/O 延迟
- **verbose 日志**：生产环境关闭 `config.verbose`，减少 stdout 开销
- **流式模式**：内存受限场景下调低 `config.memoryLimit`，强制启用流式处理

---

## 7. API 接口文档

### 7.1 配置：DocxConfig

```cpp
struct DocxConfig {
    size_t memoryLimit = 10 * 1024 * 1024;  // 内存限制（字节），默认 10MB
    std::string tempDir;                     // 临时解压目录，空则使用系统临时目录
    bool verbose = false;                    // 是否输出详细日志
    bool keepTempDir = false;                // 是否保留临时目录（调试用）
};
```

### 7.2 生命周期管理

#### open

```cpp
ErrorInfo DocxDocument::open(const std::string& path);
```

打开 docx 文件，解压到临时目录。

| 参数 | 类型 | 说明 |
|------|------|------|
| `path` | `string` | 输入 `.docx` 文件路径，必须存在且可读 |

**返回值**：`ErrorInfo`，`ok()` 返回 `true` 表示成功。

#### save

```cpp
ErrorInfo DocxDocument::save(const std::string& path);
```

将修改后的文档压缩保存为 docx 文件。

| 参数 | 类型 | 说明 |
|------|------|------|
| `path` | `string` | 输出 `.docx` 文件路径 |

**返回值**：`ErrorInfo`，`ok()` 返回 `true` 表示成功。

#### close

```cpp
void DocxDocument::close();
```

关闭文档，清理临时目录。析构时也会自动调用。

### 7.3 文本替换：replaceText

#### 批量替换

```cpp
ReplaceResult replaceText(const std::map<std::string, std::string>& replacements);
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `replacements` | `map<string,string>` | key 为占位符名称（不含 `{{}}`），value 为替换文本；key 为 `"*"` 时通配所有占位符 |

#### 单模式替换

```cpp
ReplaceResult replaceText(const std::string& pattern, const std::string& replacement);
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `pattern` | `string` | 占位符模式，支持 `"{{*}}"`（匹配所有）或 `"{{会议主题}}"`（精确匹配） |
| `replacement` | `string` | 替换文本 |

**行为**：
- 遍历 docx 中所有 `<w:p>` 段落（包括表格单元格中的段落）
- 查找 `{{key}}` 占位符，替换为对应 value
- 保留原有 `<w:rPr>` 字体/字号/样式，仅修改 `<w:t>` 文本内容
- 跨 Run 的占位符（`{{`、名称、`}}` 分布在不同 `<w:r>` 中）也能正确处理

### 7.4 富文本替换：replaceRich

```cpp
ReplaceResult replaceRich(const std::map<std::string, RichReplacement>& replacements);
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `replacements` | `map<string, RichReplacement>` | key 为占位符名称，value 为 `{content, type}` |

**RichReplacement 结构体**：

```cpp
struct RichReplacement {
    std::string content;
    ContentType type = ContentType::Plain;
};
```

**ContentType 枚举**：

| 值 | 说明 | 处理方式 |
|----|------|---------|
| `Plain` | 纯文本 | 走文本替换逻辑，仅替换 `<w:t>` 文本 |
| `HTML` | HTML 格式 | 解析 HTML，生成格式化 `<w:p>` 段落 |
| `Markdown` | Markdown 格式 | 解析 MD，生成格式化 `<w:p>` 段落 |

**HTML/Markdown 替换规则**：
- 仅替换**独占一行**的占位符（段落全文只有 `{{xx}}`）
- 删除占位符所在 `<w:p>`，在原位置插入多个新 `<w:p>`
- 新段落按 GB/T 9704-2012 公文格式渲染

### 7.5 状态查询

```cpp
bool isOpen() const;           // 文档是否已打开
bool isStreamingMode() const;  // 是否处于流式处理模式
size_t getFileSize() const;    // 获取已打开文件的大小（字节）
const DocxConfig& config() const;  // 获取配置
```

### 7.6 富文本模块独立使用

富文本解析和渲染函数可独立使用，不依赖 docx 文件操作：

```cpp
#include "docx_temp_helper/rich_content.h"

// 1. 解析 Markdown 为结构化段落
auto paragraphs = docx_temp_helper::parseMarkdown("# 标题\n正文内容");

// 2. 解析 HTML
auto paragraphs = docx_temp_helper::parseHtml("<h2>标题</h2><p>正文</p>");

// 3. 渲染为 OOXML 并插入到已有 XML 文档（DOM 模式）
pugi::xml_node parent = doc.child("w:body");
pugi::xml_node placeholder = /* 找到占位符 <w:p> */;
docx_temp_helper::renderParagraphsToXml(parent, paragraphs, placeholder);

// 4. 序列化为 XML 字符串（流式模式）
std::string xml = docx_temp_helper::serializeParagraphs(paragraphs);
```

---

## 8. 错误码说明

### 8.1 ErrorCode 枚举

| 错误码 | 值 | 说明 | 常见原因 |
|--------|---|------|---------|
| `Ok` | 0 | 成功 | - |
| `FileNotFound` | 1 | 输入文件不存在 | 路径错误 |
| `FileNotReadable` | 2 | 文件不可读 | 权限不足 |
| `InvalidDocxFormat` | 3 | 非合法 docx | 文件损坏 |
| `UnzipFailed` | 4 | 解压失败 | zip 结构损坏、minizip 错误 |
| `XmlParseFailed` | 5 | XML 解析失败 | document.xml 格式错误 |
| `XmlSaveFailed` | 6 | XML 保存失败 | 磁盘空间不足、权限问题 |
| `ZipFailed` | 7 | 压缩失败 | 磁盘空间不足 |
| `TempDirCreateFailed` | 8 | 临时目录创建失败 | `/tmp` 不可写 |
| `NoMatchFound` | 9 | 未找到匹配的占位符 | 模板中没有对应 `{{xx}}` |
| `InvalidPattern` | 10 | 占位符模式非法 | 替换映射为空 |
| `OutputWriteFailed` | 11 | 输出文件写入失败 | 路径不可写 |
| `NotOpened` | 12 | 文档未打开 | 未调用 `open()` 或已 `close()` |
| `UnknownError` | 13 | 未知错误 | - |

### 8.2 ErrorInfo 结构体

```cpp
struct ErrorInfo {
    ErrorCode code;       // 错误码
    std::string message;  // 人可读的错误描述
    std::string detail;   // 附加上下文（文件路径、错误详情等）

    bool ok() const;              // 是否成功
    std::string toString() const; // 格式化为字符串
};
```

### 8.3 ReplaceResult 结构体

```cpp
struct ReplaceResult {
    ErrorInfo error;                      // 错误信息
    std::vector<ReplaceRecord> records;   // 逐项替换记录
    int totalReplaced;                    // 总替换次数

    bool ok() const;                      // 是否成功
};

struct ReplaceRecord {
    std::string placeholder;  // 原始占位符，如 "{{会议主题}}"
    std::string replacement;  // 替换后的文本
    int count;                // 替换次数
};
```

### 8.4 错误处理示例

```cpp
docx_temp_helper::DocxDocument doc;

auto err = doc.open("input.docx");
if (!err.ok()) {
    switch (err.code) {
        case docx_temp_helper::ErrorCode::FileNotFound:
            // 提示用户检查文件路径
            break;
        case docx_temp_helper::ErrorCode::UnzipFailed:
            // 提示文件可能损坏
            break;
        default:
            std::cerr << err.toString() << std::endl;
    }
    return;
}

auto result = doc.replaceText("{{*}}", "替换文本");
if (!result.ok()) {
    std::cerr << result.error.toString() << std::endl;
    doc.close();
    return;
}

doc.save("output.docx");
doc.close();
```

---

## 9. 使用示例

### 9.1 命令行工具

```bash
# 纯文本替换：所有 {{xx}} 替换为"小红有才"
./test_docx ./联合纪要说明.docx "{{*}}" "小红有才"

# Markdown 替换：{{正文}} 替换为格式化内容
./test_docx ./联合纪要说明.docx --md "正文" "## 会议内容

本次会议讨论了以下事项：

- 项目进度汇报
- 预算审批

**重要决定**：项目延期。"

# HTML 替换：{{正文}} 替换为 HTML 内容
./test_docx ./联合纪要说明.docx --html "正文" "<h2>会议内容</h2><p>讨论了<b>项目进度</b></p>"
```

### 9.2 C++ 代码：纯文本替换

```cpp
#include "docx_temp_helper/docx_document.h"

int main() {
    docx_temp_helper::DocxConfig config;
    config.verbose = true;

    docx_temp_helper::DocxDocument doc(config);

    auto openErr = doc.open("template.docx");
    if (!openErr.ok()) {
        std::cerr << openErr.toString() << std::endl;
        return 1;
    }

    auto result = doc.replaceText("{{*}}", "小红有才");
    if (result.ok()) {
        std::cout << "替换成功，共 " << result.totalReplaced << " 处" << std::endl;
        doc.save("output.docx");
    } else {
        std::cerr << result.error.toString() << std::endl;
    }

    doc.close();
    return result.ok() ? 0 : 1;
}
```

### 9.3 C++ 代码：批量替换

```cpp
#include "docx_temp_helper/docx_document.h"

int main() {
    std::map<std::string, std::string> replacements = {
        {"会议主题", "年终总结会议"},
        {"会议时间", "2024年12月25日"},
        {"主持人", "张三"},
    };

    docx_temp_helper::DocxDocument doc;
    auto openErr = doc.open("template.docx");
    if (!openErr.ok()) return 1;

    auto result = doc.replaceText(replacements);
    if (result.ok()) {
        printf("成功替换 %d 处\n", result.totalReplaced);
        for (const auto& rec : result.records) {
            printf("  %s -> %s (x%d)\n",
                   rec.placeholder.c_str(),
                   rec.replacement.c_str(),
                   rec.count);
        }
        doc.save("output.docx");
    }

    doc.close();
    return result.ok() ? 0 : 1;
}
```

### 9.4 C++ 代码：富文本混合替换

```cpp
#include "docx_temp_helper/docx_document.h"
#include "docx_temp_helper/rich_content.h"

int main() {
    std::map<std::string, docx_temp_helper::RichReplacement> replacements;

    // 纯文本
    replacements["标题"] = {"2024年度工作报告", docx_temp_helper::ContentType::Plain};

    // Markdown 正文
    replacements["正文"] = {
        "## 一、工作总结\n\n"
        "本年度完成主要工作：\n\n"
        "1. 系统架构升级\n"
        "2. 性能优化\n"
        "3. 安全加固\n\n"
        "## 二、下年度计划\n\n"
        "继续推进**数字化转型**。",
        docx_temp_helper::ContentType::Markdown
    };

    // HTML 替换
    replacements["备注"] = {
        "<h3>注意事项</h3>"
        "<p>请各部门提前准备<b>汇报材料</b>。</p>"
        "<ul><li>材料需含<i>数据图表</i></li><li>提前3天提交</li></ul>",
        docx_temp_helper::ContentType::HTML
    };

    docx_temp_helper::DocxDocument doc;
    auto openErr = doc.open("template.docx");
    if (!openErr.ok()) return 1;

    auto result = doc.replaceRich(replacements);
    if (result.ok()) {
        doc.save("output.docx");
    }

    doc.close();
    return result.ok() ? 0 : 1;
}
```

### 9.5 C++ 代码：自定义配置（调试用）

```cpp
docx_temp_helper::DocxConfig config;
config.tempDir = "/my/custom/temp";   // 自定义临时目录
config.keepTempDir = true;             // 保留临时文件用于调试
config.verbose = true;                 // 输出详细日志
config.memoryLimit = 5 * 1024 * 1024; // 5MB 内存限制

docx_temp_helper::DocxDocument doc(config);
doc.open("input.docx");
doc.replaceText("{{*}}", "替换文本");
doc.save("output.docx");
doc.close();

// 调试：检查临时目录中的 word/document.xml
// /my/custom/temp/word/document.xml
```

---

## 10. 测试程序

项目提供了 6 个独立测试程序，覆盖所有功能：

| 测试程序 | 源文件 | 测试内容 |
|---------|--------|---------|
| `test_plain` | `test/test_plain.cpp` | 纯文本批量替换，验证占位符替换正确性和格式保留 |
| `test_markdown` | `test/test_markdown.cpp` | Markdown 富文本替换，验证标题/列表/加粗/斜体等格式 |
| `test_html` | `test/test_html.cpp` | HTML 富文本替换，验证标签解析和格式渲染 |
| `test_mixed` | `test/test_mixed.cpp` | 混合替换（纯文本 + Markdown + HTML），验证多种类型共存 |
| `test_streaming` | `test/test_streaming.cpp` | 流式处理模式，设置 1KB 内存限制强制启用流式 |
| `test_error` | `test/test_error.cpp` | 错误处理，验证文件不存在、空映射等异常场景 |

**测试数据**：
- `test/data/template.docx` - 包含多个占位符的测试模板
- `test/data/content.md` - Markdown 测试内容
- `test/data/content.html` - HTML 测试内容

**运行测试**：

```bash
cd build && cmake .. && make -j$(nproc) && ctest
```

输出示例：

```
Test project /home/hong/code/rm_back/docx_temp_helper/build
    Start 1: test_plain
1/6 Test #1: test_plain .......................   Passed    0.02 sec
    Start 2: test_markdown
2/6 Test #2: test_markdown ....................   Passed    0.02 sec
    Start 3: test_html
3/6 Test #3: test_html ........................   Passed    0.02 sec
    Start 4: test_mixed
4/6 Test #4: test_mixed .......................   Passed    0.02 sec
    Start 5: test_streaming
5/6 Test #5: test_streaming ...................   Passed    0.02 sec
    Start 6: test_error
6/6 Test #6: test_error .......................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 6
```

---

## 11. 格式标准

### GB/T 9704-2012 公文格式映射

| 元素 | 字体 | 字号 | 对齐 | 缩进 | 行距 |
|------|------|------|------|------|------|
| h1 | 方正小标宋简体 | 22pt | 居中 | 0 | 1.0 |
| h2 | 黑体 | 16pt | 左对齐 | 0 | 1.0 |
| h3 | 楷体 | 16pt | 左对齐 | 0 | 1.0 |
| h4 / 正文 | 仿宋 | 16pt | 两端对齐 | 首行缩进 2 字符 (32pt) | 1.0 |
| 列表 | 仿宋 | 16pt | 左对齐 | 悬挂缩进 | 1.0 |
| 西文 | Times New Roman | - | - | - | - |

### OOXML 单位换算

| 单位 | 换算 | 用途 |
|------|------|------|
| 字体大小 | 1pt = 2 half-points | `<w:sz>` 值（16pt = 32） |
| 缩进 | 1pt = 20 twips | `<w:ind>` 值（32pt = 640 twips） |
| 行距 | 240 = 1.0 倍 | `<w:spacing w:line="240">` |

### 支持的 Markdown 语法

| 语法 | 示例 | 渲染效果 |
|------|------|---------|
| 标题 | `# h1` ~ `#### h4` | 对应 h1~h4 |
| 加粗 | `**text**` | 加粗 |
| 斜体 | `*text*` | 斜体 |
| 无序列表 | `- item` 或 `* item` | 列表项 |
| 有序列表 | `1. item` | 有序列表 |
| 段落 | 空行分隔 | 正文段落 |

### 支持的 HTML 标签

| 标签 | 渲染效果 |
|------|---------|
| `<h1>` ~ `<h4>` | 对应 h1~h4 |
| `<p>` | 正文段落 |
| `<b>` / `<strong>` | 加粗 |
| `<i>` / `<em>` | 斜体 |
| `<u>` | 下划线 |
| `<ul>` / `<ol>` / `<li>` | 无序/有序列表 |
| `<br>` | 换行（段落内） |

---

## 12. 限制与注意事项

1. **仅处理 `word/document.xml`**：不处理页眉/页脚/尾注/批注中的占位符
2. **富文本仅替换独占段落**：HTML/Markdown 替换只对占位符独占一行的段落生效（段落全文仅 `{{xx}}`）
3. **不支持嵌套占位符**：`{{outer{{inner}}content}}` 无法正确处理
4. **不支持图片/表格**：富文本不支持插入图片或表格，仅支持段落级别的内容
5. **字形依赖**：GB/T 9704-2012 标准字体（方正小标宋简体、黑体、楷体、仿宋）需在目标系统安装
6. **流式模式限制**：流式模式下不支持表格单元格中的占位符替换（仅 DOM 模式支持）