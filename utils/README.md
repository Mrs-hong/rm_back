# utils

C++17 工具集：音频处理 + DOCX 模板替换，统一在 `utils::` 命名空间下，统一 `Result<T>` 错误模型，统一 CMake 构建（静态库 / 动态库可切换）。

本目录由 `audio_resover/` 与 `docx_temp_helper/` 两个独立项目合并而来，统一了命名空间、错误模型与构建系统，但保留各自全部功能与测试。

---

## 目录

- [1. 整体结构](#1-整体结构)
- [2. 公共模块 utils::common](#2-公共模块-utilscommon)
- [3. 音频模块 utils::audio](#3-音频模块-utilsaudio)
- [4. DOCX 模块 utils::docx](#4-docx-模块-utilsdocx)
- [5. 构建系统](#5-构建系统)
- [6. 测试](#6-测试)
- [7. 命名规范](#7-命名规范)
- [8. License](#8-license)

---

## 1. 整体结构

```
utils/
├── CMakeLists.txt                  # 顶层 CMake(选项 + 子目录管理)
├── README.md                       # 本文件
│
├── common/                         # ── 公共模块(header-only)──
│   ├── CMakeLists.txt              #   INTERFACE target utils_common
│   └── include/utils/common/
│       └── Result.hpp              #   泛型 Result<T> + void 特化
│
├── audio/                          # ── 音频处理库 utils::audio ──
│   ├── CMakeLists.txt              #   utils_audio 静态/动态库
│   ├── include/utils/audio/
│   │   ├── Audio.hpp               #   umbrella(包含这一个即可)
│   │   ├── AudioReader.hpp         #   Facade 主入口
│   │   ├── AudioError.hpp          #   ErrorCode + ToMessage + using Result
│   │   ├── AudioFormat.hpp         #   SampleFormat / ContainerFormat
│   │   ├── AudioInfo.hpp           #   元数据 POD
│   │   ├── AudioChannelConverter.hpp #  通道混合策略枚举
│   │   ├── IAudioDecoder.hpp       #   解码器策略接口
│   │   └── IAudioResampler.hpp     #   重采样策略接口
│   ├── src/                        #   实现(PIMPL 隐藏 miniaudio)
│   └── tests/
│       ├── CMakeLists.txt
│       └── test_audio_reader.cpp   #   12 个用例,自研断言宏
│
├── docx/                           # ── DOCX 模板替换库 utils::docx ──
│   ├── CMakeLists.txt              #   utils_docx 静态/动态库 + test_docx CLI
│   ├── include/utils/docx/
│   │   ├── Docx.hpp                #   umbrella
│   │   ├── docx_document.h         #   DocxDocument 类 + ErrorCode + 配置
│   │   └── rich_content.h          #   富文本解析(MD/HTML → OOXML)
│   ├── src/                        #   内部实现
│   ├── test/                       #   8 个测试程序 + data/
│   └── main.cpp                    #   test_docx CLI 工具
│
└── third_party/                   # ── 集中管理的三方依赖 ──
    ├── CMakeLists.txt              #   IMPORTED GLOBAL targets
    ├── miniaudio/                  #   header-only(MIT)
    ├── minizip/                    #   预编译静态库 + build.sh
    └── pugixml/                    #   预编译静态库 + build.sh
```

**合并要点**：

| 项 | 原 audio_resover | 原 docx_temp_helper | 合并后 |
|----|------------------|---------------------|--------|
| 命名空间 | `audio_resover` | `docx_temp_helper` | `utils::audio` / `utils::docx` |
| 错误模型 | 自有 `Result<T>` | `ErrorInfo` + `ReplaceResult` | 统一 `utils::Result<T>`(common 模块) |
| 目录名 | `audio_resover`(冗长) | `docx_temp_helper`(冗长) | `audio` / `docx` |
| 三方库 | 各自 third_party | 各自 third_party | 集中 `utils/third_party/` |
| 构建选项 | 各自 CMake | 各自 CMake | 顶层统一 `UTILS_BUILD_SHARED` 等 |

---

## 2. 公共模块 utils::common

头文件 `utils/common/Result.hpp`，header-only（INTERFACE 库 `utils_common`）。

### 设计动机

让工具集所有公开 API 返回 `Result<T>` 而非抛异常，使其可用于禁用异常的嵌入式 / 边缘运行时；错误传播在每个调用点显式可见。

### Result\<T> 接口

```cpp
namespace utils {

template <typename T> class Result {
public:
    Result() = default;                         // 默认构造为 Ok(默认值)
    bool IsOk() const;                          // mCode == 0
    explicit operator bool() const;             // 等价 IsOk(),便于 if(!r)
    const T& Value() const;                     // 仅 IsOk() 时合法
    T& Value();
    int Code() const;                           // 0=Ok,其它=错误码
    const std::string& Message() const;
    static Result Ok(T value);
    template <typename ErrorCodeT>
    static Result Fail(ErrorCodeT code, std::string msg = ""); // 枚举隐式转 int
};

template <> class Result<void> { /* 无 Value()，仅 Code/Message */ };

} // namespace utils
```

**错误码以 `int` 存储**，各子库定义自己的 `ErrorCode` 枚举（能隐式转 int），保留领域语义不混淆。各子库通过 `using utils::Result;` 把模板引入到自己的命名空间，使用户代码可直接写 `utils::audio::Result<void>` 或 `utils::docx::Result<ReplaceStats>`。

---

## 3. 音频模块 utils::audio

面向边缘设备的 C++17 音频处理库：主流格式解码、元数据查询、流式/随机 PCM 读取、可替换的下采样算法、通道下混。

基于 [miniaudio](https://github.com/mackron/miniaudio) v0.11.25（MIT，单头文件，零依赖）。

### 核心特性

| 能力 | 说明 |
|------|------|
| 主流格式 | WAV / MP3 / FLAC / Ogg Vorbis（miniaudio 提供） |
| 元数据 | 真实容器格式、采样率、采样深度、通道数、时长、文件大小、平均码率、是否损坏 |
| 数据读取 | F32 / 任意 PCM；流式顺序读 + 随机时间区间读 `[startMs, endMs)` |
| 重采样 | 仅下采样；策略模式可替换算法（默认线性），通过 `SetResampler` 注入 |
| 通道下混 | 立体声 / 多通道 → 单声道；三种策略（Average / First / Last），支持 in-place 零拷贝 |
| 清晰分层 | Facade (`AudioReader`) + Strategy (`IAudioDecoder` / `IAudioResampler`) + PIMPL |
| 边缘友好 | 不抛异常（`Result<T>`），无 STL RTTI 依赖；Linux 仅需 `pthread` + `m` |

### 公开类型一览

```cpp
namespace utils::audio {

enum class SampleFormat : int { Unknown, U8, S16, S24, S32, F32 };
enum class ContainerFormat : int { Unknown, Wav, Mp3, Flac, Vorbis, Raw };
enum class ChannelMixMode : int { Average = 0, First = 1, Last = 2 };

struct AudioInfo {
    bool IsValid, IsCorrupted;
    ContainerFormat Format;
    SampleFormat NativeSampleFormat;
    uint32_t SampleRate, Channels, BitsPerSample;
    uint64_t TotalFrames, DurationMs, FileSizeBytes;
    uint32_t AvgBitRateKbps;
};

} // namespace utils::audio
```

### AudioReader（Facade 主入口）

```cpp
class AudioReader {
public:
    // 0. 打开 / 关闭
    Result<void> Open(const std::string& filePath);
    Result<void> SetDecoder(std::unique_ptr<IAudioDecoder>);   // Open 之前替换
    bool IsOpen() const;
    void Close();

    // 1. 元数据
    const AudioInfo& GetInfo() const;

    // 2. 读取
    Result<uint64_t> ReadFrames(uint64_t frameCount, std::vector<float>& out);
    Result<uint64_t> ReadFrames(uint64_t frameCount, SampleFormat fmt,
                                std::vector<uint8_t>& out);
    Result<uint64_t> ReadTimeRangeMs(uint64_t startMs, uint64_t endMs,
                                     std::vector<float>& out);
    Result<void> SeekToMs(uint64_t ms);
    Result<void> SeekToFrame(uint64_t frameIndex);
    uint64_t GetPositionMs() const;
    uint64_t GetPositionFrames() const;
    bool HasMore() const;

    // 3. 重采样（仅下采样）
    Result<void> SetResampler(std::unique_ptr<IAudioResampler>);  // nullptr 清除
    Result<void> SetTargetSampleRate(uint32_t rate);              // 0 = 重置
    uint32_t GetTargetSampleRate() const;

    // 4. 通道下混（仅支持 → 单声道）
    Result<void> SetTargetChannels(uint32_t channels);  // 0 = 关闭；1 = 单声道
    uint32_t GetTargetChannels() const;
    void SetChannelMixMode(ChannelMixMode mode);         // 默认 Average
    ChannelMixMode GetChannelMixMode() const;
};
```

### 使用示例

```cpp
#include "utils/audio/Audio.hpp"
using namespace utils::audio;

// 例 1：打开并打印元数据
AudioReader reader;
auto r = reader.Open("song.mp3");
if (!r) { std::cerr << r.Message() << "\n"; return 1; }
const AudioInfo& info = reader.GetInfo();
std::cout << "rate=" << info.SampleRate << " ch=" << info.Channels << "\n";

// 例 2：流式读 F32
std::vector<float> buf;
while (reader.HasMore()) {
    auto r = reader.ReadFrames(4096, buf);
    if (!r) break;
    ProcessSamples(buf.data(), buf.size());
}

// 例 3：下采样到 22050 Hz + 单声道
reader.SetTargetSampleRate(22050);
reader.SetTargetChannels(1);
reader.ReadFrames(2205, mono);   // mono.size() == 2205
```

### 错误码表（audio 域）

错误码按命名空间分区：

| Code | 枚举值 | 含义 |
|------|--------|------|
| 0 | `Ok` | 成功 |
| 1001 | `FileNotFound` | 文件不存在 |
| 1002 | `FileOpenFailed` | 打开文件失败 |
| 1003 | `FileReadFailed` | 读文件失败 |
| 2001 | `UnknownFormat` | 无法识别的音频格式 |
| 2002 | `UnsupportedFormat` | 不支持的格式 |
| 2003 | `CorruptedFile` | 文件已损坏 |
| 2004 | `DecodeFailed` | 解码失败 |
| 2005 | `SeekFailed` | 跳转失败 |
| 3001 | `InvalidArgument` | 参数非法 |
| 3002 | `OutOfRange` | 位置超出范围 |
| 3003 | `NotOpened` | 尚未调用 `Open` |
| 3004 | `AlreadyOpened` | 已经打开 |
| 4001 | `UnsupportedResampleDirection` | 仅支持下采样（不允许上采样） |
| 4002 | `ResamplerNotSet` | 未配置重采样器 |
| 4003 | `ResamplerInitFailed` | 重采样器初始化失败 |
| 4004 | `ResampleFailed` | 重采样失败 |
| 5001 | `UnsupportedChannelDirection` | 不支持上混（target > source） |
| 5002 | `ChannelMixFailed` | 通道混合失败 |
| 9001 | `InternalError` | 内部错误 |
| 9002 | `NotImplemented` | 未实现 |

可通过 `utils::audio::ToMessage(code)` 获取简短英文描述。

### 扩展：自定义解码器 / 重采样器

继承 `IAudioDecoder` / `IAudioResampler` 实现策略类，通过 `SetDecoder` / `SetResampler` 在 `Open` 前注入。详见各接口头文件注释。

---

## 4. DOCX 模块 utils::docx

DOCX 模板占位符替换与文档生成库：将 `.docx` 中的 `{{xx}}` 占位符替换为文本或富文本，保留原有排版格式；也可基于空白模板生成完整文档。

### 核心特性

| 能力 | 说明 |
|------|------|
| 模板占位符替换 | `{{xx}}` → 文本 / 富文本，保留 `<w:rPr>` 字体字号样式 |
| 跨 Run 替换 | Word 将 `{{` `名称` `}}` 拆到不同 `<w:r>` 中也能正确处理 |
| 富文本渲染 | HTML/Markdown → GB/T 9704-2012 公文格式段落 |
| 双处理模式 | DOM 模式（常规）与流式模式（大文件），按 `memoryLimit` 自动选择 |
| 文档生成 | 从空白模板 + 标题 + 正文（Plain/MD/HTML）生成完整文档 |
| ZIP 工具 | 独立 `ZipCompress` / `ZipExtract` 函数，跨平台兼容 |
| 免费闭源商用 | minizip (zlib) + pugixml (MIT) |

### DocxDocument 类

```cpp
namespace utils::docx {

struct DocxConfig {
    size_t memoryLimit = 10 * 1024 * 1024;  // 内存限制(字节),默认 10MB
    std::string tempDir;                     // 临时解压目录,空则用系统临时目录
    bool verbose = false;                    // 详细日志
    bool keepTempDir = false;                // 保留临时目录(调试)
};

class DocxDocument {
public:
    DocxDocument();
    explicit DocxDocument(const DocxConfig& config);
    ~DocxDocument();                          // 析构自动清理临时目录

    // 生命周期
    Result<void> Open(const std::string& path);
    Result<void> Save(const std::string& path);
    void Close();

    // 文本替换
    Result<ReplaceStats> ReplaceText(const std::map<std::string, std::string>& replacements);
    Result<ReplaceStats> ReplaceText(const std::string& pattern, const std::string& replacement);

    // 富文本替换
    Result<ReplaceStats> ReplaceRich(const std::map<std::string, RichReplacement>& replacements);

    // 文档生成(从空白模板)
    Result<ReplaceStats> GenerateDocument(const std::string& title,
                                          const std::string& bodyContent,
                                          ContentType contentType);

    // 状态查询
    bool IsOpen() const;
    bool IsStreamingMode() const;
    size_t GetFileSize() const;
    const DocxConfig& Config() const;
};

} // namespace utils::docx
```

### ReplaceStats 与富文本类型

```cpp
namespace utils::docx {

enum class ContentType { Plain, HTML, Markdown };

struct RichReplacement {
    std::string content;
    ContentType type = ContentType::Plain;
};

struct ReplaceRecord {
    std::string placeholder;   // 如 "{{会议主题}}"
    std::string replacement;
    int count = 0;             // 替换次数
};

struct ReplaceStats {
    std::vector<ReplaceRecord> records;
    int totalReplaced = 0;
};

} // namespace utils::docx
```

> 旧版本的 `ErrorInfo` + `ReplaceResult` 已合并为 `utils::Result<ReplaceStats>`：失败时 `Code()/Message()` 提供错误信息，成功时 `Value()` 返回 `ReplaceStats`。

### 使用示例

```cpp
#include "utils/docx/Docx.hpp"

// 例 1：纯文本批量替换
std::map<std::string, std::string> replacements = {
    {"会议主题", "年终总结会议"},
    {"会议时间", "2024年12月25日"},
};
utils::docx::DocxDocument doc;
if (!doc.Open("template.docx").IsOk()) return 1;
auto result = doc.ReplaceText(replacements);
if (result.IsOk()) {
    std::cout << "替换 " << result.Value().totalReplaced << " 处\n";
    doc.Save("output.docx");
}
doc.Close();

// 例 2：富文本混合替换(MD + HTML + Plain)
std::map<std::string, utils::docx::RichReplacement> rich;
rich["标题"] = {"2024年度工作报告", utils::docx::ContentType::Plain};
rich["正文"] = {"## 一、工作总结\n本年度完成主要工作...", utils::docx::ContentType::Markdown};
rich["备注"] = {"<h3>注意事项</h3><p>请提前准备<b>材料</b></p>", utils::docx::ContentType::HTML};
doc.ReplaceRich(rich);

// 例 3：从空白模板生成文档
doc.Open("templates/默认模板.docx");
doc.GenerateDocument("季度工作总结会议纪要", mdContent,
                     utils::docx::ContentType::Markdown);
doc.Save("output.docx");
```

### ZIP 工具函数

```cpp
// 压缩：目录或文件 → ZIP（Deflate 压缩）
Result<void> ZipCompress(const std::string& srcPath,
                         const std::string& outputPath,
                         const std::string& zipName);
// 打包：目录或文件 → ZIP（仅存储不压缩，速度优先）
Result<void> ZipStore(const std::string& srcPath,
                      const std::string& outputPath,
                      const std::string& zipName);
// 解压：ZIP → 目录
Result<void> ZipExtract(const std::string& zipPath,
                        const std::string& destDir);
```

`srcPath` 为目录时递归打包保持结构；为文件时打包单文件。生成的 ZIP 使用正斜杠路径分隔符，Windows/Mac/Linux 系统自带工具均可直接解压。`ZipCompress` 使用 `Z_DEFLATED` 标准压缩；`ZipStore` 使用 Store 方法（不压缩），速度更快，适用于数据本身已压缩或对速度要求高的场景。

### 错误码表（docx 域）

按域分区：1xxx=文件/IO，2xxx=XML/处理（与 audio 域保持一致的分区风格）：

| Code | 枚举值 | 含义 |
|------|--------|------|
| 0 | `Ok` | 成功 |
| 1001 | `FileNotFound` | 输入文件不存在 |
| 1002 | `FileNotReadable` | 文件无法读取 |
| 1003 | `InvalidDocxFormat` | 非合法 docx（zip 结构损坏） |
| 1004 | `UnzipFailed` | 解压失败 |
| 1005 | `TempDirCreateFailed` | 临时目录创建失败 |
| 1006 | `OutputWriteFailed` | 输出文件写入失败 |
| 2001 | `XmlParseFailed` | XML 解析失败 |
| 2002 | `XmlSaveFailed` | XML 保存失败 |
| 2003 | `ZipFailed` | 压缩失败 |
| 2004 | `NoMatchFound` | 未找到匹配的占位符 |
| 2005 | `InvalidPattern` | 占位符模式非法 |
| 2006 | `NotOpened` | 文档未打开 |
| 2099 | `UnknownError` | 未知错误 |

可通过 `utils::docx::ToMessage(code)` 获取简短英文描述（与枚举名一致）。

### CLI 工具 test_docx

构建后生成 `test_docx` 可执行文件：

```bash
# 纯文本替换
./test_docx ./template.docx "{{*}}" "小红有才"

# Markdown 替换
./test_docx ./template.docx --md "正文" "## 标题\n正文"

# HTML 替换
./test_docx ./template.docx --html "正文" "<h2>标题</h2><p>正文</p>"
```

### 富文本渲染规则（GB/T 9704-2012）

| 元素 | 字体 | 字号 | 对齐 | 缩进 |
|------|------|------|------|------|
| h1 | 方正小标宋简体 | 22pt | 居中 | 无 |
| h2 | 黑体 | 16pt | 左对齐 | 无 |
| h3 | 楷体 | 16pt | 左对齐 | 无 |
| h4 / 正文 | 仿宋 | 16pt | 两端对齐 | 首行缩进 32pt |
| 列表 | 仿宋 | 16pt | 左对齐 | 悬挂缩进 |
| 西文 | Times New Roman | 同字号 | - | - |

支持的 Markdown：`# h1` ~ `#### h4`、`**bold**`、`*italic*`、`- item` / `* item`、`1. item`、空行分段。
支持的 HTML：`<h1>`~`<h4>`、`<p>`、`<b>`/`<strong>`、`<i>`/`<em>`、`<u>`、`<ul>`/`<ol>`/`<li>`、`<br>`。

### 限制与注意事项

1. 仅处理 `word/document.xml`，不处理页眉/页脚/尾注/批注
2. 富文本仅替换**独占一行**的占位符（段落全文仅 `{{xx}}`）
3. 不支持嵌套占位符 `{{outer{{inner}}}}`
4. 富文本不支持图片/表格，仅段落级内容
5. GB/T 9704-2012 标准字体需在目标系统安装
6. `GenerateDocument` 始终使用 DOM 模式（空白模板很小）

---

## 5. 构建系统

### 依赖

| 名称 | 版本 | License | 用途 | 来源 |
|------|------|---------|------|------|
| miniaudio | 0.11.25 | MIT | 音频解码/重采样 | header-only，vendored |
| minizip | 1.3.1 | zlib | ZIP 压缩/解压 | 预编译静态库，build.sh 拉取源码 |
| pugixml | master | MIT | XML 解析 | 预编译静态库，build.sh 拉取源码 |
| zlib | >= 1.2.11 | zlib | minizip 依赖 | 系统库 `sudo apt install zlib1g-dev` |
| CMake | >= 3.16 | BSD-3 | 构建系统 | 系统自带 |
| C++ 编译器 | >= C++17 | - | 编译 | g++ / clang++ |

### 首次构建：编译三方库

```bash
cd utils/third_party/pugixml && ./build.sh
cd ../minizip && ./build.sh
```

`build.sh` 自动从 GitHub（失败回退 Gitee）拉取源码，用 `-fPIC` 编译为静态库（既能链接到静态主库，也能链接到动态主库），安装到 `include/` + `lib/`，清理中间产物。

> 注意：build.sh 只用 `-fPIC`，**不要**加 `-fPIE`。`-fPIE` 会覆盖 `-fPIC` 生成 PIE 专用代码，其 `R_X86_64_PC32` 重定位无法用于共享库链接。

### 构建主项目

一次配置即同时产出静态库（`.a`）和动态库（`.so`），无需切换选项：

```bash
cd utils
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

产物（同一 build 目录下）：

| 类型 | audio | docx |
|------|-------|------|
| 静态库 | `build/audio/libutils_audio.a` | `build/docx/libutils_docx.a` |
| 动态库 | `build/audio/libutils_audio.so` | `build/docx/libutils_docx.so` |

测试与 CLI：

- `build/audio/tests/test_audio_reader`
- `build/docx/test_docx`（CLI）
- `build/docx/test_plain`、`test_markdown`、`test_html`、`test_mixed`、`test_streaming`、`test_error`、`test_generate`、`test_zip`

### CMake 选项

| 选项 | 默认 | 说明 |
|------|------|------|
| `UTILS_BUILD_TESTS` | `ON` | 是否构建测试程序 |
| `UTILS_WARNINGS_AS_ERRORS` | `OFF` | 把警告当错误（`-Werror`） |
| `UTILS_AUDIO_TEST_FILE` | 自动 | audio 测试默认音频文件路径（默认 `../一小时空音频.wav`） |

### CMake target 命名

每个库同时构建静态和动态两个 target，共用 `OUTPUT_NAME`，因此产物文件名相同（仅扩展名不同）：

| target | 类型 | 别名 | 产物 |
|--------|------|------|------|
| `utils_audio_static` | STATIC | `utils::audio` | `libutils_audio.a` |
| `utils_audio_shared` | SHARED | `utils::audio_shared` | `libutils_audio.so` |
| `utils_docx_static` | STATIC | `utils::docx` | `libutils_docx.a` |
| `utils_docx_shared` | SHARED | `utils::docx_shared` | `libutils_docx.so` |

- 不带后缀的别名（`utils::audio` / `utils::docx`）默认指向**静态库**，便于下游直接链接无需处理 RPATH
- 测试程序与 `test_docx` CLI 均链接静态库
- 源码统一用 `-fPIC` 编译，静态库也能被链接进动态库

### 在下游项目中使用

```cmake
# add_subdirectory 引入后,按需选择静态或动态
add_subdirectory(utils)

# 链接静态库(默认,无需 RPATH)
target_link_libraries(your_app PRIVATE utils::audio utils::docx)

# 或链接动态库
target_link_libraries(your_app PRIVATE utils::audio_shared utils::docx_shared)
```

```cpp
// 用户代码只需包含 umbrella 头文件(与链接静态/动态无关)
#include "utils/audio/Audio.hpp"
#include "utils/docx/Docx.hpp"
```

### 交叉编译

边缘设备交叉编译时，修改 `build.sh` 中的 `CC`/`CXX`/`AR` 环境变量：

```bash
CC=aarch64-linux-gnu-gcc CXX=aarch64-linux-gnu-g++ ./build.sh
```

CMake 交叉编译：

```bash
cmake .. -DCMAKE_TOOLCHAIN_FILE=toolchain-aarch64.cmake
```

---

## 6. 测试

共 9 个测试程序，覆盖两个模块全部功能，自研断言宏（无 GoogleTest 依赖）：

| # | 测试 | 源文件 | 内容 | 断言数 |
|---|------|--------|------|--------|
| 1 | `audio_reader_tests` | audio/tests/test_audio_reader.cpp | 音频解码/元数据/流式读/重采样/通道下混/错误处理 | ~690 |
| 2 | `test_plain` | docx/test/test_plain.cpp | 纯文本批量替换 + 通配符替换 | 11 |
| 3 | `test_markdown` | docx/test/test_markdown.cpp | Markdown 富文本替换 | 10 |
| 4 | `test_html` | docx/test/test_html.cpp | HTML 富文本替换 | 10 |
| 5 | `test_mixed` | docx/test/test_mixed.cpp | 混合替换（纯文本 + Markdown） | 10 |
| 6 | `test_streaming` | docx/test/test_streaming.cpp | 流式处理模式（1KB 内存限制强制启用） | 13 |
| 7 | `test_error` | docx/test/test_error.cpp | 错误处理（文件不存在/空映射/未打开等） | 6 |
| 8 | `test_generate` | docx/test/test_generate.cpp | 从空白模板生成文档 | 28 |
| 9 | `test_zip` | docx/test/test_zip.cpp | ZIP 工具函数 | 15 |

### 运行测试

```bash
cd build
ctest --output-on-failure
```

**audio 测试数据**：`test_audio_reader` 需要一个 wav 测试文件，默认路径 `../一小时空音频.wav`。若文件不在该位置，通过 CMake 选项指定：

```bash
cmake -DUTILS_AUDIO_TEST_FILE=/path/to/your/audio.wav ..
```

也可运行时通过 argv[1] 覆盖：`./test_audio_reader /path/to/audio.wav`

**docx 测试数据**：`docx/test/data/` 目录下包含 `template.docx`、`content.md`、`content.html`，已随仓库提供，无需额外准备。

测试通过后会保留输出 `.docx` 文件（位于 `docx/test/data/`），供人工检查替换效果。

---

## 7. 命名规范

| 类别 | 规则 | 示例 |
|------|------|------|
| 函数 / 方法 | 大驼峰 (PascalCase) | `ReadFrames` / `SeekToMs` |
| 局部变量 / 参数 | 小驼峰 (camelCase) | `frameCount` / `filePath` |
| 成员变量 | `m` + 大驼峰 | `mDecoder` / `mTargetSampleRate` |
| 类型 / 类 | 大驼峰 (PascalCase) | `AudioReader` / `DocxDocument` |
| 枚举值 | 大驼峰 (PascalCase) | `SampleFormat::F32` / `ErrorCode::FileNotFound` |
| 命名空间 | 小写 + 域分隔 | `utils` / `utils::audio` / `utils::docx` |
| 头文件 | 与类型同名 | `AudioReader.hpp` / `docx_document.h` |

代码风格由 `.clang-format` 配置约束（基于 LLVM，4 空格缩进、Linux 大括号、100 列）。

---

## 8. License

本工具集源码采用 **MIT License**，与所有依赖兼容，可闭源商用：

- 本库源码：MIT
- miniaudio：MIT（`third_party/miniaudio/`，Copyright (c) David Reid）
- pugixml：MIT（`third_party/pugixml/`）
- minizip：zlib License（`third_party/minizip/`）
- zlib：zlib License（系统库）
