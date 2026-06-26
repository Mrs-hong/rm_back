# docx_temp_helper

DOCX 模板占位符替换库 —— 面向边缘设备的轻量 C++17 解决方案。

---

## 目录

- [1. 概述](#1-概述)
- [2. 三方库选择](#2-三方库选择)
- [3. 目录结构](#3-目录结构)
- [4. 构建方式](#4-构建方式)
- [5. 架构设计：函数式 vs 面向对象](#5-架构设计函数式-vs-面向对象)
- [6. 性能分析](#6-性能分析)
- [7. API 接口文档](#7-api-接口文档)
- [8. 错误码说明](#8-错误码说明)
- [9. 使用示例](#9-使用示例)
- [10. 格式标准](#10-格式标准)
- [11. 限制与注意事项](#11-限制与注意事项)

---

## 1. 概述

本库提供对 `.docx` 文件中 `{{xx}}` 占位符的替换功能：

- **纯文本替换**：将 `{{会议主题}}` 替换为指定文本，保留原有字体/字号/段落格式
- **富文本替换**：将 `{{正文}}` 替换为 Markdown/HTML 格式化内容，生成符合公文标准的段落

核心特性：
- 跨 Run 占位符替换（Word 将 `{{` `名称` `}}` 拆分到不同 `<w:r>` 中也能正确处理）
- 保留 OOXML 中所有 `<w:rPr>` 格式属性
- 支持 GB/T 9704-2012 公文格式标准
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
│   ├── docx_replacer.h                         # 公开 API（3个接口 + 错误设计）
│   └── rich_content.h                          # 富文本模块（ContentType/RichRun/RichParagraph）
│
├── src/
│   ├── docx_replacer.cpp                       # 主实现：占位符查找 + 逐字符替换
│   ├── rich_content.cpp                        # MD/HTML解析 + OOXML段落渲染
│   ├── zip_utils.h / zip_utils.cpp             # ZIP 工具（minizip 封装）
│   └── xml_utils.h / xml_utils.cpp             # XML 工具（pugixml 封装）
│
├── third_party/
│   ├── minizip/
│   │   ├── build.sh                            # 拉取源码并编译（GitHub → Gitee 回退）
│   │   ├── include/                            # zip.h, unzip.h, ioapi.h
│   │   └── lib/                                # libminizip.a
│   └── pugixml/
│       ├── build.sh                            # 拉取源码并编译
│       ├── include/                            # pugixml.hpp, pugiconfig.hpp
│       └── lib/                                # libpugixml.a
│
├── templates/summary/
│   └── 联合行文模板.docx                        # 测试用模板
│
└── main.cpp                                    # test_docx 测试程序
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
cd third_party/minizip && ./build.sh

# 第2步：编译主项目
cd ../../build
cmake ..
make -j$(nproc)

# 第3步：运行测试
./test_docx ./联合纪要说明.docx "{{*}}" "小红有才"
```

### 4.3 build.sh 说明

三方库的 `build.sh` 脚本会：
1. 从 GitHub 拉取源码（失败自动回退 Gitee 镜像）
2. 编译为静态库（`.a`）
3. 安装头文件到 `include/`，库文件到 `lib/`
4. 清理中间产物

minizip 使用稳定版 zlib v1.3.1 的 `contrib/minizip`，避免 master 分支的额外依赖。

---

## 5. 架构设计：函数式 vs 面向对象

### 5.1 当前选择：自由函数 + namespace

本库采用**自由函数（free function）**设计，而非面向对象（class）设计：

```cpp
// 当前设计：自由函数
namespace docx_temp_helper {
    ReplaceResult replacePlaceholders(input, pattern, replacement, output, options);
    ReplaceResult replacePlaceholdersRich(input, map, output, options);
}
```

### 5.2 为什么选择函数式？

| 考量维度 | 函数式（当前） | 面向对象（class） |
|---------|--------------|-----------------|
| **使用场景** | 一次性操作：输入→替换→输出 | 多步操作：打开→多次修改→保存 |
| **状态管理** | 无状态，线程安全 | 持有 XML 文档、临时目录等状态 |
| **调用成本** | 直接调用，无对象构造 | 需构造对象，管理生命周期 |
| **C 互操作** | 易于 C wrapper 封装 | 需额外适配层 |
| **资源管理** | 函数内部 RAII（自动清理） | 需 destructor 释放 |
| **扩展性** | 新增函数即可 | 可继承/多态，但此场景无多态需求 |

**核心理由**：

1. **本场景是无状态变换**：输入文件 → 替换 → 输出文件，无需跨调用保持状态。函数式天然适合无状态操作，类似 `std::transform`、`std::sort` 的设计哲学。

2. **边缘设备 simplicity 优先**：嵌入式/边缘场景下，对象生命周期管理是额外复杂度。函数调用更直接，减少出错可能。

3. **RAII 已足够**：临时目录、XML 文档等资源在函数内部通过 RAII 自动管理，不需要 class destructor。

4. **C 互操作友好**：边缘设备常需 C/C++ 混合编程，自由函数易于通过 `extern "C"` 封装为 C 接口。

### 5.3 什么时候应该用 class？

如果未来出现以下需求，建议重构为 class：

- **多次操作同一文档**：避免重复解压/压缩（当前每次调用都解压+压缩）
- **流式处理**：边读取边替换，不全部加载到内存
- **插件化**：不同替换策略通过多态切换

示例（未来可选的 class 设计）：

```cpp
// 未来可选的 class 设计（当前未实现）
class DocxDocument {
public:
    bool open(const std::string& path);   // 解压一次
    int  replaceText(const map<string,string>& replacements);
    int  replaceRich(const map<string,RichReplacement>& replacements);
    bool save(const std::string& path);   // 压缩一次
    ~DocxDocument();                       // 自动清理临时目录
};
```

**当前函数式设计不影响未来重构**：内部实现已模块化（`collectRuns`、`replaceInParagraph`、`parseMarkdown` 等），可平滑迁移到 class 方法。

---

## 6. 性能分析

### 6.1 函数式 vs 面向对象：性能差异

**结论：两者性能无实质差异。**

| 因素 | 函数式 | 面向对象 | 差异 |
|------|--------|---------|------|
| 调用开销 | 直接调用 | `this` 指针传入（等同） | 0 |
| 内联优化 | 编译器可内联自由函数 | 编译器可内联成员函数 | 0 |
| 内存分配 | 栈上局部变量 | 对象成员（通常也在栈上） | 0 |
| 虚函数 | 无 | 若用 virtual 则有虚表开销 | 函数式略优（但本场景无需虚函数） |

在 `-O2` 优化下，编译器对自由函数和成员函数生成几乎相同的机器码。

### 6.2 实际性能瓶颈

真正影响性能的是 I/O 和算法，不是函数 vs class：

| 阶段 | 耗时占比 | 说明 |
|------|---------|------|
| 解压 docx | ~30% | minizip 解压 ZIP，I/O 密集 |
| XML 解析 | ~15% | pugixml DOM 解析 word/document.xml |
| 占位符查找+替换 | ~10% | 逐字符遍历 runs，O(n) 线性扫描 |
| XML 保存 | ~10% | pugixml 序列化回 XML |
| 压缩 docx | ~35% | minizip 压缩 ZIP，CPU 密集 |

### 6.3 优化建议

- **批量替换**：使用 `map<string,string>` 一次传入所有替换项，避免多次调用（每次调用都解压+压缩）
- **临时目录**：`options.tempDir` 指定 SSD 路径，减少 I/O 延迟
- **verbose 日志**：生产环境关闭 `options.verbose`，减少 stdout 开销

---

## 7. API 接口文档

### 7.1 接口1：批量纯文本替换

```cpp
ReplaceResult replacePlaceholders(
    const std::string& inputPath,                        // 输入 docx 路径
    const std::map<std::string, std::string>& replacements, // 占位符名→替换文本
    const std::string& outputPath,                       // 输出 docx 路径
    const ReplaceOptions& options = {}                   // 可选参数
);
```

**参数说明**：

| 参数 | 类型 | 说明 |
|------|------|------|
| `inputPath` | `string` | 输入 `.docx` 文件路径，必须存在且可读 |
| `replacements` | `map<string,string>` | key 为占位符名称（不含 `{{}}`），value 为替换文本；key 为 `"*"` 时通配所有 |
| `outputPath` | `string` | 输出 `.docx` 文件路径 |
| `options` | `ReplaceOptions` | 可选参数，见下表 |

**ReplaceOptions 字段**：

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `tempDir` | `string` | `""` | 临时解压目录，空则使用 `/tmp/docx_<pid>_XXXXXX` |
| `preserveFormatting` | `bool` | `true` | 是否保留原始格式（当前始终为 true） |
| `verbose` | `bool` | `false` | 是否输出详细日志到 stdout |
| `keepTempDir` | `bool` | `false` | 是否保留临时目录（调试用） |

**返回值**：`ReplaceResult`，包含错误信息和替换记录。

**行为**：
- 遍历 docx 中所有 `<w:p>` 段落（包括表格单元格中的段落）
- 查找 `{{key}}` 占位符，替换为对应 value
- 保留原有 `<w:rPr>` 字体/字号/样式，仅修改 `<w:t>` 文本内容
- 跨 Run 的占位符（`{{`、名称、`}}` 分布在不同 `<w:r>` 中）也能正确处理

**示例**：

```cpp
#include "docx_temp_helper/docx_replacer.h"

// 示例：批量替换多个占位符
std::map<std::string, std::string> replacements = {
    {"会议主题", "2024年第四季度工作总结会议"},
    {"会议时间", "2024年12月25日 14:00"},
    {"会议地点", "三楼会议室"},
    {"主持人", "张三"},
    {"参会人员", "全体员工"},
    {"备注", "请准时参加"}
};

docx_temp_helper::ReplaceOptions opts;
opts.verbose = true;

auto result = docx_temp_helper::replacePlaceholders(
    "input.docx", replacements, "output.docx", opts);

if (result.ok()) {
    std::cout << "替换成功，共替换 " << result.totalReplaced << " 处" << std::endl;
} else {
    std::cerr << result.error.toString() << std::endl;
}
```

### 7.2 接口2：单模式纯文本替换

```cpp
ReplaceResult replacePlaceholders(
    const std::string& inputPath,     // 输入 docx 路径
    const std::string& pattern,       // 占位符模式
    const std::string& replacement,   // 替换文本
    const std::string& outputPath,    // 输出 docx 路径
    const ReplaceOptions& options = {}
);
```

**参数说明**：

| 参数 | 类型 | 说明 |
|------|------|------|
| `pattern` | `string` | `{{*}}` 匹配所有占位符；`{{会议主题}}` 匹配特定占位符；`*` 或 `会议主题` 也可 |
| `replacement` | `string` | 替换文本 |

**示例**：

```cpp
// 通配替换：所有 {{xx}} 替换为同一文本
auto result = docx_temp_helper::replacePlaceholders(
    "input.docx", "{{*}}", "小红有才", "output.docx");

// 精确替换：仅替换 {{会议主题}}
auto result2 = docx_temp_helper::replacePlaceholders(
    "input.docx", "{{会议主题}}", "年终总结会议", "output.docx");
```

### 7.3 接口3：富文本批量替换

```cpp
ReplaceResult replacePlaceholdersRich(
    const std::string& inputPath,
    const std::map<std::string, RichReplacement>& replacements,
    const std::string& outputPath,
    const ReplaceOptions& options = {}
);
```

**参数说明**：

| 参数 | 类型 | 说明 |
|------|------|------|
| `replacements` | `map<string, RichReplacement>` | key 为占位符名称，value 为 `{content, type}` |

**RichReplacement 结构体**：

```cpp
struct RichReplacement {
    std::string content;                    // 内容字符串
    ContentType type = ContentType::Plain;  // 内容类型
};
```

**ContentType 枚举**：

| 值 | 说明 | 处理方式 |
|----|------|---------|
| `Plain` | 纯文本 | 走接口1逻辑，仅替换 `<w:t>` 文本 |
| `HTML` | HTML 格式 | 解析 HTML，生成格式化 `<w:p>` 段落，替换占位符所在整个段落 |
| `Markdown` | Markdown 格式 | 解析 MD，生成格式化 `<w:p>` 段落，替换占位符所在整个段落 |

**行为差异**：

| 类型 | 替换粒度 | 格式 | 适用场景 |
|------|---------|------|---------|
| Plain | 文本级（修改 `<w:t>`） | 保留原格式 | 简单文本替换 |
| HTML/MD | 段落级（替换整个 `<w:p>`） | 生成新格式 | 长正文、结构化内容 |

**HTML/Markdown 替换规则**：
- 仅替换**独占一行**的占位符（段落全文只有 `{{xx}}`）
- 删除占位符所在 `<w:p>`，在原位置插入多个新 `<w:p>`
- 新段落按 GB/T 9704-2012 公文格式渲染

**示例**：

```cpp
#include "docx_temp_helper/docx_replacer.h"
#include "docx_temp_helper/rich_content.h"

// 混合替换：纯文本 + Markdown + HTML
std::map<std::string, docx_temp_helper::RichReplacement> replacements;

// 纯文本替换
replacements["会议主题"] = {"年终总结会议", docx_temp_helper::ContentType::Plain};
replacements["会议时间"] = {"2024年12月25日", docx_temp_helper::ContentType::Plain};

// Markdown 替换（正文）
replacements["正文"] = {
    "## 会议内容\n\n"
    "本次会议讨论了以下事项：\n\n"
    "- 项目进度汇报\n"
    "- 下阶段工作安排\n"
    "- 预算审批\n\n"
    "**重要决定**：项目延期至下月。",
    docx_temp_helper::ContentType::Markdown
};

// HTML 替换
replacements["备注"] = {
    "<h3>注意事项</h3>"
    "<p>请各部门提前准备<b>汇报材料</b>。</p>"
    "<ul><li>材料需含<i>数据图表</i></li><li>提前3天提交</li></ul>",
    docx_temp_helper::ContentType::HTML
};

auto result = docx_temp_helper::replacePlaceholdersRich(
    "input.docx", replacements, "output.docx");
```

### 7.4 富文本模块独立使用

富文本解析和渲染函数可独立使用，不依赖 docx 文件操作：

```cpp
#include "docx_temp_helper/rich_content.h"

// 1. 解析 Markdown 为结构化段落
auto paragraphs = docx_temp_helper::parseMarkdown("# 标题\n正文内容");

// 2. 解析 HTML
auto paragraphs = docx_temp_helper::parseHtml("<h2>标题</h2><p>正文</p>");

// 3. 渲染为 OOXML 并插入到已有 XML 文档
pugi::xml_node parent = doc.child("w:body");
pugi::xml_node placeholder = /* 找到占位符 <w:p> */;
docx_temp_helper::renderParagraphsToXml(parent, paragraphs, placeholder);
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
| `UnknownError` | 12 | 未知错误 | - |

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
auto result = docx_temp_helper::replacePlaceholders(/* ... */);

if (!result.ok()) {
    switch (result.error.code) {
        case docx_temp_helper::ErrorCode::FileNotFound:
            // 提示用户检查文件路径
            break;
        case docx_temp_helper::ErrorCode::NoMatchFound:
            // 提示模板中没有对应占位符
            break;
        case docx_temp_helper::ErrorCode::UnzipFailed:
            // 提示文件可能损坏
            break;
        default:
            std::cerr << result.error.toString() << std::endl;
    }
}
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

### 9.2 C++ 代码：纯文本批量替换

```cpp
#include "docx_temp_helper/docx_replacer.h"

int main() {
    std::map<std::string, std::string> replacements = {
        {"会议主题", "年终总结会议"},
        {"会议时间", "2024年12月25日"},
        {"主持人", "张三"},
    };

    auto result = docx_temp_helper::replacePlaceholders(
        "template.docx", replacements, "output.docx");

    if (result.ok()) {
        printf("成功替换 %d 处\n", result.totalReplaced);
        for (const auto& rec : result.records) {
            printf("  %s -> %s\n", rec.placeholder.c_str(), rec.replacement.c_str());
        }
    }
    return result.ok() ? 0 : 1;
}
```

### 9.3 C++ 代码：富文本混合替换

```cpp
#include "docx_temp_helper/docx_replacer.h"
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

    auto result = docx_temp_helper::replacePlaceholdersRich(
        "template.docx", replacements, "output.docx");

    return result.ok() ? 0 : 1;
}
```

### 9.4 C++ 代码：自定义临时目录（调试用）

```cpp
docx_temp_helper::ReplaceOptions opts;
opts.tempDir = "/my/custom/temp";  // 自定义临时目录
opts.keepTempDir = true;            // 保留临时文件用于调试
opts.verbose = true;                // 输出详细日志

auto result = docx_temp_helper::replacePlaceholders(
    "input.docx", "{{*}}", "替换文本", "output.docx", opts);

// 调试：检查临时目录中的 word/document.xml
// /my/custom/temp/word/document.xml
```

---

## 10. 格式标准

### 10.1 纯文本替换格式保留

纯文本替换保留以下 OOXML 属性：
- `<w:rPr>` 中的所有字体设置（`w:rFonts` 的 `w:eastAsia`、`w:ascii`、`w:hAnsi`）
- 字号（`w:sz`、`w:szCs`）
- 加粗（`w:b`）、斜体（`w:i`）、下划线（`w:u`）
- 段落属性（`<w:pPr>` 中的缩进、对齐、行距等）
- 表格结构、文档结构完全不动

### 10.2 富文本生成格式（GB/T 9704-2012）

| 元素 | 中文字体 | 西文字体 | 字号 | 对齐 | 缩进 |
|------|---------|---------|------|------|------|
| h1 (#) | 方正小标宋简体 | Times New Roman | 22pt | 居中 | 无 |
| h2 (##) | 黑体 | Times New Roman | 16pt | 左对齐 | 无 |
| h3 (###) | 楷体 | Times New Roman | 16pt | 左对齐 | 无 |
| h4 (####) | 仿宋 | Times New Roman | 16pt | 左对齐 | 无 |
| 正文段落 | 仿宋 | Times New Roman | 16pt | 两端对齐 | 首行缩进 32pt |
| 无序列表 | 仿宋 | Times New Roman | 16pt | 左对齐 | 悬挂缩进 16pt |
| 有序列表 | 仿宋 | Times New Roman | 16pt | 左对齐 | 悬挂缩进 16pt |

行距统一为 1.0 倍。标题段后间距 8pt。

### 10.3 OOXML 单位换算

| 单位 | 换算关系 |
|------|---------|
| 字号 (w:sz) | 半磅，如 16pt = 32 |
| 缩进 (w:ind) | twips，1pt = 20twips，如 32pt = 640twips |
| 行距 (w:spacing w:line) | 240 = 1.0 倍行距 |
| 段后间距 (w:spacing w:after) | twips，如 8pt = 160twips |

---

## 11. 限制与注意事项

### 11.1 当前限制

1. **Markdown 支持**：仅支持基础语法（标题、加粗、斜体、列表、段落），不支持表格、代码块、图片、链接
2. **HTML 支持**：仅支持基础标签（p, h1-h4, b/strong, i/em, u, ul/ol/li, br），不支持 table, img, a, div
3. **富文本替换范围**：HTML/Markdown 仅替换**独占一行**的占位符（段落全文只有 `{{xx}}`）；非独占的占位符走纯文本逻辑
4. **表格内占位符**：表格单元格中的 `{{xx}}` 支持纯文本替换，但不支持富文本替换（因为会改变表格结构）
5. **并发安全**：自由函数本身无状态，但临时目录路径需各调用不同（默认使用 PID+随机数，不冲突）
6. **大文件**：全部加载到内存，不适合超大 docx（>100MB）

### 11.2 注意事项

1. **三方库编译**：首次使用前必须执行 `third_party/*/build.sh` 编译 minizip 和 pugixml
2. **zlib 依赖**：minizip 依赖系统 zlib（`zlib1g-dev`）
3. **网络要求**：`build.sh` 需要从 GitHub/Gitee 拉取源码，首次构建需联网
4. **文件编码**：docx 内部使用 UTF-8，传入的替换文本也应为 UTF-8
5. **输出覆盖**：`outputPath` 如已存在会被覆盖，不报错
6. **临时目录清理**：默认自动清理，设置 `keepTempDir=true` 可保留用于调试

---

## 许可证

本库代码可免费闭源商用。三方库许可证：
- minizip: zlib License
- pugixml: MIT License
