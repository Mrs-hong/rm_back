# docx_temp_helper 实现计划

## 概述

在 `/home/hong/code/rm_back/docx_temp_helper/` 下实现一个 C++ DOCX 模板占位符替换库，用于在边缘设备上对 `.docx` 文件中的 `{{xx}}` 占位符进行文本替换并生成新文档，不改变原有排版样式。

## 三方库选择

### ZIP 库：minizip（zlib License）

**选择理由：**
- **许可证**：zlib License，允许免费闭源商用，仅需保留版权声明
- **体积极小**：仅 `zip.c` + `unzip.c` + `ioapi.c` 三个源文件，适合边缘设备
- **零依赖**：仅依赖 zlib（大多数系统自带），无其他运行时依赖
- **功能完备**：支持读写 ZIP 文件，满足 docx 解压/压缩需求
- **成熟稳定**：随 zlib 分发，广泛应用于工业级项目

### XML 库：pugixml（MIT License）

**选择理由：**
- **许可证**：MIT License，允许免费闭源商用
- **高性能**：DOM 解析速度远超 tinyxml2，适合处理 OOXML（document.xml 可达数百 KB）
- **体积小**：仅 `pugixml.cpp` + `pugixml.hpp` + `pugiconfig.hpp` 三个文件
- **支持修改**：可直接在 DOM 树上修改节点文本并保存，这是替换占位符的核心需求
- **零依赖**：无任何外部依赖，C++17 兼容
- **API 友好**：支持 XPath 查询，便于定位 `<w:r>`、`<w:t>` 节点

### 不选其他库的原因

| 库 | 不选原因 |
|---|---|
| libzip | BSD 许可可商用，但依赖较多、体量偏大，边缘设备不够轻量 |
| minizip-ng | minizip 的改进版，但引入额外依赖（如 OpenSSL），增加复杂度 |
| tinyxml2 | MIT 许可可商用，但 DOM 解析慢、内存占用高，不适合处理大 XML |
| RapidXML | Boost 许可可商用，但仅支持只读解析（修改需额外处理），API 较老 |
| expat | MIT 许可，但 SAX 模式不适合修改 XML，需自行构建 DOM 树 |

## 目录结构

```
docx_temp_helper/
├── CMakeLists.txt                    # 顶层构建文件
├── include/
│   └── docx_temp_helper/
│       └── docx_replacer.h           # 公开 API 头文件
├── src/
│   ├── docx_replacer.cpp             # 主实现：占位符替换逻辑
│   ├── zip_utils.h                   # ZIP 工具声明
│   ├── zip_utils.cpp                 # ZIP 工具实现（基于 minizip）
│   ├── xml_utils.h                   # XML 工具声明
│   └── xml_utils.cpp                 # XML 工具实现（基于 pugixml）
├── third_party/
│   ├── minizip/                      # minizip 源码
│   │   ├── zip.c / zip.h
│   │   ├── unzip.c / unzip.h
│   │   ├── ioapi.c / ioapi.h
│   │   ├── crypt.c / crypt.h
│   │   └── minizip_license.txt
│   └── pugixml/                      # pugixml 源码
│       ├── pugixml.cpp
│       ├── pugixml.hpp
│       ├── pugiconfig.hpp
│       └── pugixml_license.txt
└── main.cpp                          # 测试程序入口
```

## 公开 API 设计

```cpp
// include/docx_temp_helper/docx_replacer.h
#pragma once

#include <string>
#include <map>
#include <vector>

namespace docx_temp_helper {

// ───────── 错误码与错误信息 ─────────

/// 错误码枚举
enum class ErrorCode {
    Ok = 0,               ///< 成功
    FileNotFound,         ///< 输入文件不存在
    FileNotReadable,      ///< 输入文件无法读取
    InvalidDocxFormat,    ///< 非合法 docx 文件（zip 结构损坏）
    UnzipFailed,          ///< 解压失败
    XmlParseFailed,       ///< XML 解析失败
    XmlSaveFailed,        ///< XML 保存失败
    ZipFailed,            ///< 压缩失败
    TempDirCreateFailed,  ///< 临时目录创建失败
    NoMatchFound,         ///< 未找到匹配的占位符
    InvalidPattern,       ///< 占位符模式格式非法
    OutputWriteFailed,    ///< 输出文件写入失败
    UnknownError          ///< 未知错误
};

/// 错误信息结构体，承载详细错误描述
struct ErrorInfo {
    ErrorCode code = ErrorCode::Ok;  ///< 错误码
    std::string message;             ///< 人可读的错误描述
    std::string detail;              ///< 附加上下文（如文件路径、XML 节点路径等）

    /// 是否成功
    bool ok() const { return code == ErrorCode::Ok; }

    /// 转为字符串（用于日志/打印）
    std::string toString() const;
};

// ───────── 替换选项 ─────────

/// 替换选项
struct ReplaceOptions {
    std::string tempDir;            ///< 临时解压目录，为空则使用系统临时目录
    bool preserveFormatting = true;  ///< 是否保留原始格式（默认保留）
    bool verbose = false;            ///< 是否输出详细日志
    bool keepTempDir = false;        ///< 是否保留临时目录（调试用，默认清理）
};

// ───────── 替换结果 ─────────

/// 单个占位符的替换记录
struct ReplaceRecord {
    std::string placeholder;  ///< 原始占位符，如 "{{会议主题}}"
    std::string replacement;  ///< 替换后的文本
    int count = 0;            ///< 替换次数（同一占位符可能出现多次）
};

/// 整体替换结果
struct ReplaceResult {
    ErrorInfo error;                       ///< 错误信息（ok=成功）
    std::vector<ReplaceRecord> records;    ///< 逐个占位符的替换记录
    int totalReplaced = 0;                 ///< 总替换次数

    /// 是否成功
    bool ok() const { return error.ok(); }
};

// ───────── 公开接口 ─────────

/// 【接口1】批量替换：传入 map<string, string> 一次替换多个占位符
/// @param inputPath    输入 docx 文件路径
/// @param replacements 占位符名称→替换文本 的映射
///                     key 为占位符名称（不含 {{}}），如 "会议主题"
///                     value 为替换文本，如 "小红有才"
///                     若 key 为 "*"，则匹配所有 {{xx}} 并统一替换为对应 value
/// @param outputPath   输出 docx 文件路径
/// @param options      可选参数（临时目录等）
/// @return 替换结果（含错误信息与逐项记录）
ReplaceResult replacePlaceholders(
    const std::string& inputPath,
    const std::map<std::string, std::string>& replacements,
    const std::string& outputPath,
    const ReplaceOptions& options = {}
);

/// 【接口2】单模式替换：传入模式字符串和替换文本
/// @param inputPath   输入 docx 文件路径
/// @param pattern     占位符模式，支持 "{{*}}"（匹配所有）或 "{{具体名称}}"
/// @param replacement 替换文本
/// @param outputPath  输出 docx 文件路径
/// @param options     可选参数（临时目录等）
/// @return 替换结果（含错误信息与逐项记录）
ReplaceResult replacePlaceholders(
    const std::string& inputPath,
    const std::string& pattern,
    const std::string& replacement,
    const std::string& outputPath,
    const ReplaceOptions& options = {}
);

} // namespace docx_temp_helper
```

### 错误处理设计

**错误码分类**：

| 类别 | 错误码 | 触发场景 |
|------|--------|---------|
| 文件 I/O | `FileNotFound` | 输入 docx 路径不存在 |
|          | `FileNotReadable` | 输入文件无读取权限 |
|          | `OutputWriteFailed` | 输出路径不可写 |
| 格式/解压 | `InvalidDocxFormat` | 文件不是合法 zip/docx |
|          | `UnzipFailed` | minizip 解压过程中出错 |
|          | `ZipFailed` | 压缩回 docx 时出错 |
| XML 处理 | `XmlParseFailed` | pugixml 解析 document.xml 失败 |
|          | `XmlSaveFailed` | 保存修改后的 XML 失败 |
| 临时目录 | `TempDirCreateFailed` | 无法创建临时目录 |
| 业务逻辑 | `NoMatchFound` | 文档中未找到匹配的占位符 |
|          | `InvalidPattern` | 模式格式非法（如缺少 `{{` 或 `}}`）|
| 其他 | `UnknownError` | 未预期的异常 |

**ErrorInfo 设计要点**：
- `code`：程序化判断使用，便于调用方 switch 处理
- `message`：简洁描述，如 "解压失败"
- `detail`：附加上下文，如 "minizip 返回错误码 -1, 文件: word/document.xml"
- `toString()`：格式化输出，如 `[UnzipFailed] 解压失败 | minizip 返回错误码 -1`

**ReplaceResult 设计要点**：
- 每个占位符的替换次数记录在 `records` 中，便于调试
- `totalReplaced` 汇总所有替换次数
- `error.ok()` 为 true 时，`records` 才有效

## 核心算法：跨 Run 占位符替换

### 问题分析

OOXML 中，`{{会议主题}}` 通常被 Word 拆分为 3 个 `<w:r>`（run）元素：

```xml
<w:r><w:rPr>样式A</w:rPr><w:t>{{</w:t></w:r>
<w:r><w:rPr>样式B</w:rPr><w:t>会议主题</w:t></w:r>
<w:r><w:rPr>样式A</w:rPr><w:t>}}</w:t></w:r>
```

有时中间还夹杂 `<w:proofErr>`（拼写检查标记）。

### 替换策略

保留所有 `<w:r>` 和 `<w:rPr>` 节点不动，仅修改 `<w:t>` 文本：

1. 第一个 run 的 `<w:t>` 文本：`{{` → `` （清空）
2. 第二个 run 的 `<w:t>` 文本：`会议主题` → `小红有才` （替换，保留样式B）
3. 第三个 run 的 `<w:t>` 文本：`}}` → `` （清空）

### 算法步骤

```
1. 解压 docx 到临时目录
2. 加载 word/document.xml
3. 用 pugixml 解析 XML
4. 遍历所有 <w:p>（段落）节点：
   a. 收集段落下所有 <w:r> 子节点及其 <w:t> 文本
   b. 拼接全文，记录每个 run 的文本在全文中的起止位置
   c. 在全文中搜索 {{...}} 模式（支持通配 * 或精确匹配）
   d. 对每个匹配：
      - 根据位置映射，找到 {{ 所在的 run(s) → 清空文本
      - 找到占位符名称所在的 run(s) → 替换为 replacement
      - 找到 }} 所在的 run(s) → 清空文本
   e. 保存修改后的 XML
5. 重新压缩临时目录为 docx
6. 清理临时目录
```

## 实现步骤

### Step 1: 准备三方库源码

将 minizip 和 pugixml 的源码文件放入 `third_party/`。

- minizip：从 zlib 源码包中提取 `contrib/minizip/` 下的文件
- pugixml：使用单文件版本 `pugixml.cpp` + `pugixml.hpp` + `pugiconfig.hpp`

### Step 2: 编写 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.10)
project(docx_temp_helper CXX)
set(CMAKE_CXX_STANDARD 17)

# --- minizip ---
add_library(minizip STATIC
    third_party/minizip/zip.c
    third_party/minizip/unzip.c
    third_party/minizip/ioapi.c
)
target_include_directories(minizlp PUBLIC third_party/minizip)
find_package(ZLIB REQUIRED)
target_link_libraries(minizip PUBLIC ZLIB::ZLIB)

# --- pugixml ---
add_library(pugixml STATIC third_party/pugixml/pugixml.cpp)
target_include_directories(pugixml PUBLIC third_party/pugixml)

# --- docx_temp_helper 库 ---
add_library(docx_temp_helper STATIC
    src/docx_replacer.cpp
    src/zip_utils.cpp
    src/xml_utils.cpp
)
target_include_directories(docx_temp_helper PUBLIC include)
target_link_libraries(docx_temp_helper PUBLIC minizip pugixml)

# --- 测试程序 ---
add_executable(test_docx main.cpp)
target_link_libraries(test_docx PRIVATE docx_temp_helper)
```

### Step 3: 实现 zip_utils

```cpp
// src/zip_utils.h
namespace docx_temp_helper {

/// 解压 ZIP 文件到指定目录
/// @param zipPath  ZIP/docx 文件路径
/// @param destDir  目标目录
/// @return true=成功
bool unzipToDir(const std::string& zipPath, const std::string& destDir);

/// 将目录压缩为 ZIP 文件
/// @param srcDir   源目录
/// @param zipPath  输出 ZIP/docx 文件路径
/// @return true=成功
bool zipDir(const std::string& srcDir, const std::string& zipPath);

} // namespace docx_temp_helper
```

### Step 4: 实现 xml_utils

```cpp
// src/xml_utils.h
#include <pugixml.hpp>
#include <string>
#include <vector>

namespace docx_temp_helper {

/// Run 信息结构体
struct RunInfo {
    pugi::xml_node runNode;    ///< <w:r> 节点
    std::string text;          ///< <w:t> 文本
    size_t startPos;           ///< 在段落全文中的起始位置
    size_t endPos;             ///< 在段落全文中的结束位置
};

/// 收集段落中所有 run 的文本信息
/// @param paragraph <w:p> 节点
/// @param outRuns   输出的 run 信息列表
/// @return 段落全文
std::string collectRuns(pugi::xml_node paragraph, std::vector<RunInfo>& outRuns);

/// 设置 run 的 <w:t> 文本
/// @param runNode  <w:r> 节点
/// @param text     新文本
void setRunText(pugi::xml_node runNode, const std::string& text);

} // namespace docx_temp_helper
```

### Step 5: 实现 docx_replacer

核心替换逻辑（两个接口共享内部实现）：

1. 参数校验：检查输入文件存在性、模式合法性 → 填充 `ErrorInfo`
2. 创建临时目录（`options.tempDir` 或 `/tmp/docx_XXXXXX`）
3. 调用 `zip_utils::unzipToDir()` 解压 → 失败返回 `UnzipFailed`
4. 用 pugixml 加载 `word/document.xml` → 失败返回 `XmlParseFailed`
5. 遍历所有 `<w:p>` 节点：
   a. 调用 `collectRuns()` 获取全文和 run 位置映射
   b. **接口1（map）**：对 map 中每个 key，在全文中搜索 `{{key}}`，精确匹配
   c. **接口2（pattern）**：若 pattern 含 `*`，正则搜索 `\{\{[^}]+\}\}`；否则按精确 key 搜索
   d. 对每个匹配，根据位置映射修改对应 run 的 `<w:t>` 文本
   e. 记录 `ReplaceRecord`（placeholder + replacement + count）
6. 保存 XML → 失败返回 `XmlSaveFailed`
7. 调用 `zip_utils::zipDir()` 重新压缩 → 失败返回 `ZipFailed`
8. 清理临时目录（除非 `keepTempDir=true`）
9. 返回 `ReplaceResult`（含错误信息与替换记录）

**接口2内部委托接口1**：将 `pattern` 解析为 map 后调用接口1，避免重复实现。

### Step 6: 实现 main.cpp

```cpp
#include "docx_temp_helper/docx_replacer.h"
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "用法: " << argv[0]
                  << " <输入docx> <模式如{{*}}> <替换文本>" << std::endl;
        std::cerr << "示例: " << argv[0]
                  << " ./联合纪要说明.docx \"{{*}}\" \"小红有才\"" << std::endl;
        return 1;
    }

    std::string inputPath = argv[1];
    std::string pattern = argv[2];
    std::string replacement = argv[3];

    // 输出文件名：在原文件名后加 _replaced
    std::string outputPath = inputPath;
    size_t dotPos = outputPath.rfind('.');
    if (dotPos != std::string::npos) {
        outputPath = outputPath.substr(0, dotPos) + "_replaced.docx";
    }

    docx_temp_helper::ReplaceOptions options;
    options.verbose = true;

    // 调用单模式替换接口
    auto result = docx_temp_helper::replacePlaceholders(
        inputPath, pattern, replacement, outputPath, options);

    if (result.ok()) {
        std::cout << "替换成功！输出文件: " << outputPath << std::endl;
        std::cout << "总替换次数: " << result.totalReplaced << std::endl;
        for (const auto& rec : result.records) {
            std::cout << "  " << rec.placeholder
                      << " -> " << rec.replacement
                      << " (x" << rec.count << ")" << std::endl;
        }
        return 0;
    } else {
        std::cerr << "替换失败：" << result.error.toString() << std::endl;
        return 1;
    }
}
```

## 验证步骤

1. 将测试文件 `联合行文模板.docx` 复制为 `联合纪要说明.docx` 用于测试
2. 编译：`cd docx_temp_helper && mkdir build && cd build && cmake .. && make`
3. 运行：`./test_docx ./联合纪要说明.docx "{{*}}" "小红有才"`
4. 检查输出文件 `联合纪要说明_replaced.docx`：
   - 所有 8 个 `{{xx}}` 应被替换为"小红有才"
   - 字体（方正小标宋_GBK / 仿宋 / Times New Roman）、字号等样式保留
   - 表格结构、段落结构不变
   - `<w:proofErr>` 等辅助元素保留

## 假设与决策

1. **通配符 `{{*}}`**：`*` 匹配 `{{` 和 `}}` 之间的任意文本（非贪婪）
2. **也支持精确匹配**：如 `"{{会议主题}}"` 只替换该特定占位符
3. **临时目录**：由 `ReplaceOptions.tempDir` 指定，为空时使用 `/tmp/docx_XXXXXX`
4. **仅处理 document.xml**：占位符替换只发生在 `word/document.xml`，不处理页眉页脚
5. **跨段落占位符**：不支持 `{{` 和 `}}` 分布在不同段落的情况（实际 docx 不会出现）
6. **编码**：统一使用 UTF-8，minizip 和 pugixml 均原生支持
