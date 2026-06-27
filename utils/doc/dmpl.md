# utils 工具集 - 结构设计与 API 详细分析

> 本文档由 `audio_resover/README.md` 与 `docx_temp_helper/README.md` 合并整理而来，
> 详细分析两个子库的结构设计、架构分层、API 接口、数据流与关键设计决策，
> 作为 `utils/README.md`（快速上手向）的补充参考。

---

## 目录

- [1. 整体结构](#1-整体结构)
- [2. 公共模块 utils::common](#2-公共模块-utilscommon)
- [3. 音频模块 utils::audio](#3-音频模块-utilsaudio)
  - [3.1 目录结构](#31-目录结构)
  - [3.2 架构与设计模式](#32-架构与设计模式)
  - [3.3 模块依赖关系](#33-模块依赖关系)
  - [3.4 公开类型](#34-公开类型)
  - [3.5 AudioReader 接口详解](#35-audioreader-接口详解)
  - [3.6 策略接口](#36-策略接口)
  - [3.7 数据流](#37-数据流)
  - [3.8 关键设计决策](#38-关键设计决策)
  - [3.9 使用示例](#39-使用示例)
  - [3.10 错误码表](#310-错误码表)
  - [3.11 扩展指南](#311-扩展指南)
- [4. DOCX 模块 utils::docx](#4-docx-模块-utilsdocx)
  - [4.1 目录结构](#41-目录结构)
  - [4.2 分层设计](#42-分层设计)
  - [4.3 核心类 DocxDocument](#43-核心类-docxdocument)
  - [4.4 双处理模式](#44-双处理模式)
  - [4.5 占位符替换实现](#45-占位符替换实现)
  - [4.6 富文本渲染实现](#46-富文本渲染实现)
  - [4.7 文档生成实现](#47-文档生成实现)
  - [4.8 API 接口文档](#48-api-接口文档)
  - [4.9 使用示例](#49-使用示例)
  - [4.10 命令行工具](#410-命令行工具)
  - [4.11 格式标准](#411-格式标准)
  - [4.12 错误码表](#412-错误码表)
  - [4.13 限制与注意事项](#413-限制与注意事项)
- [5. 合并对照表](#5-合并对照表)
- [6. 命名规范](#6-命名规范)

---

## 1. 整体结构

```
utils/
├── CMakeLists.txt                  # 顶层 CMake(选项 + 子目录管理)
├── README.md                       # 快速上手文档
├── doc/
│   └── dmpl.md                     # 本文件(详细设计与 API 分析)
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

### 与旧版本的对应关系

| 旧 audio_resover | 旧 docx_temp_helper | 合并后 |
|------------------|---------------------|--------|
| `Result<T>`（自带） | `ErrorInfo` + `ReplaceResult` | 统一 `utils::Result<T>`（common 模块） |
| `AudioErrorCode` | `ErrorCode` | 各域保留自己的 `ErrorCode` 枚举 |

---

## 3. 音频模块 utils::audio

面向边缘设备的 C++17 音频处理库：主流格式解码、元数据查询、流式/随机 PCM 读取、可替换的下采样算法、通道下混。

基于 [miniaudio](https://github.com/mackron/miniaudio) v0.11.25（MIT，单头文件，零依赖）。

### 核心特性

| #   | 能力                                     | 说明                                                                                 |
| --- | ---------------------------------------- | ------------------------------------------------------------------------------------ |
| 0   | 主流音频格式                             | WAV / MP3 / FLAC / Ogg Vorbis（由 miniaudio 提供）                                   |
| 1   | 元数据                                   | 真实容器格式、采样率、采样深度、通道数、时长、文件大小、平均码率、是否被破坏         |
| 2   | 数据读取                                 | F32 / 任意 PCM 格式；流式顺序读 + 随机时间区间读 `[startMs, endMs)`                  |
| 3   | 重采样                                   | 仅下采样；策略模式可替换算法（默认线性），通过 `SetResampler` 注入                   |
| 3.5 | 通道下混                                 | 立体声 / 多通道 → 单声道；三种混合策略（Average / First / Last），支持 in-place 零拷贝 |
| 4   | 错误码                                   | 20 个细分错误码，按命名空间分区（1xxx 文件 / 2xxx 解码 / 3xxx 参数 / 4xxx 重采样 / 5xxx 通道 / 9xxx 内部） |
| 5   | 清晰分层                                 | Facade (`AudioReader`) + Strategy (`IAudioDecoder` / `IAudioResampler`) + PIMPL      |
| 6   | 边缘友好                                 | 不抛异常（`Result<T>`），无 STL RTTI 依赖，可禁用异常；Linux 仅需 `pthread` + `m`    |

### 3.1 目录结构

```
audio/
├── CMakeLists.txt              # CMake 构建配置（C++17，静态/动态库）
│
├── include/utils/audio/
│   ├── Audio.hpp               # umbrella 头文件（include 这一个即可）
│   ├── AudioReader.hpp         # 高层 facade（用户主入口）
│   ├── AudioChannelConverter.hpp # 通道混合策略枚举
│   ├── AudioInfo.hpp           # 元数据 POD
│   ├── AudioError.hpp          # 错误码 + Result<T>
│   ├── AudioFormat.hpp         # SampleFormat / ContainerFormat 枚举
│   ├── IAudioDecoder.hpp       # 解码器策略接口（用于扩展）
│   └── IAudioResampler.hpp     # 重采样策略接口（用于扩展）
│
├── src/                        # 内部实现(PIMPL 隐藏 miniaudio)
│   ├── AudioReader.cpp         # facade 实现
│   ├── ChannelMixer.{hpp,cpp}  # 通道混合器（私有工具，F32 交错布局）
│   ├── MiniaudioDecoder.{hpp,cpp}  # 默认 IAudioDecoder 实现（私有头）
│   ├── MiniaudioResampler.{hpp,cpp}# 默认 IAudioResampler 实现（私有头）
│   ├── MiniaudioGuard.hpp      # miniaudio 初始化 RAII 钩子（预留）
│   └── miniaudio_impl.cpp      # 唯一定义 MINIAUDIO_IMPLEMENTATION 的 TU
│
└── tests/
    ├── CMakeLists.txt
    └── test_audio_reader.cpp   # 12 个测试用例，自研断言宏（无 GoogleTest）
```

### 3.2 架构与设计模式

```
                ┌─────────────────────────────────────────┐
                │              用户代码                    │
                └───────────────┬─────────────────────────┘
                                │ 仅依赖公开 API
                                ▼
            ┌───────────────────────────────────────────────┐
            │           AudioReader (Facade)                │
            │   ─ Open / Close / GetInfo                    │
            │   ─ ReadFrames (F32 / 任意 PCM)               │
            │   ─ ReadTimeRangeMs / SeekToMs                │
            │   ─ SetResampler / SetTargetSampleRate        │
            │   ─ SetTargetChannels / SetChannelMixMode      │
            └───┬───────────────┬───────────────┬───────────┘
                │ 组合           │ 内部工具      │ 可选组合
                ▼               ▼               ▼
        ┌─────────────┐  ┌──────────────┐  ┌─────────────────────┐
        │ IAudioDecoder│  │ ChannelMixer │  │  IAudioResampler    │
        │ (Strategy)  │  │ (内部静态类) │  │  (Strategy 接口)    │
        └──────┬──────┘  └──────┬───────┘  └──────────┬──────────┘
               │ 默认实现        │ F32 下混            │ 默认实现
               ▼                 ▼                     ▼
        ┌─────────────────┐ ┌──────────┐    ┌─────────────────────┐
        │ MiniaudioDecoder│ │ in-place │    │  MiniaudioResampler │
        │ (PIMPL, 私有)   │ │ stride / │    │  (PIMPL, 私有)      │
        └────────┬────────┘ │ 平均混合 │    └──────────┬──────────┘
                 │           └──────────┘               │
                 ▼                                      ▼
        ┌─────────────────────────────────────────────────┐
        │         miniaudio (MIT, 单头文件)               │
        │   ma_decoder / ma_data_converter / ma_pcm_*      │
        └─────────────────────────────────────────────────┘
```

| 模式        | 应用位置                                          | 目的                                       |
| ----------- | ------------------------------------------------- | ------------------------------------------ |
| Facade      | `AudioReader`                                     | 为外部提供统一简化入口，隔离策略切换细节   |
| Strategy    | `IAudioDecoder` / `IAudioResampler`               | 解码器 / 重采样器可替换，降低耦合          |
| PIMPL       | `MiniaudioDecoder` / `MiniaudioResampler`         | 隐藏 miniaudio 私有依赖，缩短编译时间       |
| Value-like  | `Result<T>`                                       | 用值类型承载错误，避免异常                 |
| RAII        | `MiniaudioGuard`（预留）                          | 全局资源获取 / 释放（当前为空操作）        |
| 内部工具类  | `ChannelMixer`（私有静态方法）                    | 通道混合逻辑不暴露为接口；算法固定，三种模式已枚举覆盖 |

### 3.3 模块依赖关系

```
include/utils/audio/Audio.hpp     <- umbrella，用户唯一入口
        │
        ├── AudioError.hpp         (无依赖)
        ├── AudioFormat.hpp        (无依赖)
        ├── AudioInfo.hpp          -> AudioFormat.hpp
        ├── AudioChannelConverter.hpp -> AudioFormat.hpp
        ├── IAudioDecoder.hpp      -> AudioError.hpp / AudioFormat.hpp / AudioInfo.hpp
        ├── IAudioResampler.hpp     -> AudioError.hpp / AudioFormat.hpp
        └── AudioReader.hpp         -> 上述所有

src/AudioReader.cpp                       -> AudioReader.hpp + 私有头
src/ChannelMixer.cpp                      -> ChannelMixer.hpp（F32 交错下混）
src/MiniaudioDecoder.cpp                  -> MiniaudioDecoder.hpp + miniaudio.h
src/MiniaudioResampler.cpp                -> MiniaudioResampler.hpp + miniaudio.h
src/miniaudio_impl.cpp                    -> miniaudio.h (定义 MINIAUDIO_IMPLEMENTATION)
```

### 3.4 公开类型

#### 枚举与 POD

```cpp
namespace utils::audio {

// 采样格式（决定 PCM 在内存中的布局）
enum class SampleFormat : int { Unknown, U8, S16, S24, S32, F32 };

// 容器格式（按文件内容嗅探，与扩展名无关）
enum class ContainerFormat : int { Unknown, Wav, Mp3, Flac, Vorbis, Raw };

// 通道混合策略（仅当 targetChannels == 1 且 source > 1 时生效）
enum class ChannelMixMode : int { Average = 0, First = 1, Last = 2 };

// 元数据 POD（只读）
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

#### AudioInfo 字段说明

| 字段                  | 类型            | 说明                                            |
| --------------------- | --------------- | ----------------------------------------------- |
| `IsValid`             | `bool`          | 是否成功解析                                    |
| `IsCorrupted`         | `bool`          | 解码过程中是否检测到错误                        |
| `Format`              | `ContainerFormat` | 真实容器格式（按文件头嗅探，与扩展名无关）     |
| `NativeSampleFormat`  | `SampleFormat`  | 文件原始采样格式                                |
| `SampleRate`          | `uint32_t`      | 采样率 (Hz)                                     |
| `Channels`            | `uint32_t`      | 通道数                                          |
| `BitsPerSample`       | `uint32_t`      | 每样本位数                                      |
| `TotalFrames`         | `uint64_t`      | 总帧数（1 帧 = 每通道 1 个采样）                |
| `DurationMs`          | `uint64_t`      | 时长（毫秒）                                    |
| `FileSizeBytes`       | `uint64_t`      | 文件字节数                                      |
| `AvgBitRateKbps`      | `uint32_t`      | 平均码率（仅压缩格式；PCM 为 0）                |

### 3.5 AudioReader 接口详解

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

**方法详细说明**：

| 方法                                                                                                       | 返回                  | 说明                                       |
| ---------------------------------------------------------------------------------------------------------- | --------------------- | ------------------------------------------ |
| `Open(const std::string& filePath)`                                                                        | `Result<void>`        | 打开文件，使用默认 `MiniaudioDecoder`       |
| `SetDecoder(std::unique_ptr<IAudioDecoder>)`                                                               | `Result<void>`        | 替换解码器（必须在 `Open` 前调用）          |
| `IsOpen() const`                                                                                           | `bool`                | 是否已打开                                  |
| `Close()`                                                                                                  | `void`                | 关闭并释放资源                              |
| `GetInfo() const`                                                                                          | `const AudioInfo&`    | 元数据引用                                  |
| `ReadFrames(uint64_t frameCount, std::vector<float>& out)`                                                | `Result<uint64_t>`    | 顺序读 F32，返回实际帧数                    |
| `ReadFrames(uint64_t frameCount, SampleFormat fmt, std::vector<uint8_t>& out)`                            | `Result<uint64_t>`    | 顺序读任意 PCM 格式（禁用重采样时可用）     |
| `ReadTimeRangeMs(uint64_t startMs, uint64_t endMs, std::vector<float>& out)`                              | `Result<uint64_t>`    | 随机读 `[startMs, endMs)`                   |
| `SeekToMs(uint64_t ms)` / `SeekToFrame(uint64_t frameIndex)`                                                | `Result<void>`        | 跳转                                       |
| `GetPositionMs() const` / `GetPositionFrames() const`                                                      | `uint64_t`            | 当前位置                                   |
| `HasMore() const`                                                                                          | `bool`                | 是否还有未读数据                           |
| `SetResampler(std::unique_ptr<IAudioResampler>)`                                                          | `Result<void>`        | 注入自定义重采样器，`nullptr` 清除         |
| `SetTargetSampleRate(uint32_t rate)`                                                                       | `Result<void>`        | 设置目标采样率（仅下采样），自动创建默认重采样器 |
| `GetTargetSampleRate() const`                                                                              | `uint32_t`            | 当前目标采样率                              |
| `SetTargetChannels(uint32_t channels)`                                                                     | `Result<void>`        | 通道下混（仅支持 `0` 关闭 / `1` 单声道），必须在 `Open` 后调用 |
| `GetTargetChannels() const`                                                                                | `uint32_t`            | 当前目标通道数（`0` 表示直通原始通道）       |
| `SetChannelMixMode(ChannelMixMode mode)`                                                                   | `void`                | 选择下混策略（Average / First / Last），默认 Average |
| `GetChannelMixMode() const`                                                                                | `ChannelMixMode`      | 当前下混策略                                |

### 3.6 策略接口

#### IAudioDecoder（解码器策略接口）

```cpp
class IAudioDecoder {
public:
    virtual ~IAudioDecoder() = default;
    virtual Result<void> Open(const std::string& filePath) = 0;
    virtual bool IsOpen() const = 0;
    virtual void Close() = 0;
    virtual const AudioInfo& GetInfo() const = 0;
    virtual Result<uint64_t> ReadFrames(uint64_t frameCount,
                                        SampleFormat outFormat,
                                        void* outBuffer) = 0;
    virtual Result<void> SeekToFrame(uint64_t frameIndex) = 0;
    virtual uint64_t GetPositionFrames() const = 0;
};
```

#### IAudioResampler（重采样策略接口）

```cpp
class IAudioResampler {
public:
    virtual ~IAudioResampler() = default;
    virtual Result<void> Configure(uint32_t inSampleRate,
                                   uint32_t outSampleRate,
                                   uint32_t channels,
                                   SampleFormat format) = 0;
    virtual Result<uint64_t> Process(const void* inBuffer, uint64_t inFrameCount,
                                    void* outBuffer, uint64_t outFrameCountCap) = 0;
    virtual uint64_t EstimateOutFrames(uint64_t inFrameCount) const = 0;
};
```

**契约**：

- 仅下采样（`outSampleRate <= inSampleRate`）
- 采样格式与通道数保持不变
- `Process` 可增量调用，内部状态跨调用保留

### 3.7 数据流

#### 直通读取（无重采样 / 无通道下混）

```
用户 ReadFrames(N, out)
   └─> AudioReader 检查 mResampler == nullptr
       └─> mDecoder->ReadFrames(N, F32, out.data())
           └─> ma_decoder_read_pcm_frames(...)
```

#### 通道下混读取（启用 SetTargetChannels(1)）

```
用户 ReadFrames(N, out)
   └─> AudioReader 检查 mTargetChannels == 1 && srcChannels > 1
       └─> mDecoder->ReadFrames(N, F32, out.data())   // 读立体声到 out
       └─> ChannelMixer::MixToMono(out.data(), N, srcChannels, mode, out.data())
           └─> in-place 转换：out[i] = (in[i*C..i*C+C-1]) 平均/首/末
       └─> out.resize(N)   // 从 N*srcChannels 缩到 N（单声道）
```

#### 重采样 + 通道下混（顺序：重采样 → 通道下混）

```
用户 ReadFrames(N, out)
   └─> AudioReader 估算输入帧 inputEstimate = N * srcRate / dstRate + 32
       └─> mDecoder->ReadFrames(inputEstimate, F32, inBuf)
       └─> mResampler->Process(inBuf, inFrames, out, N)
           └─> ma_data_converter_process_pcm_frames(...)
       └─> （若启用通道下混）
           └─> ChannelMixer::MixToMono(out, N, srcChannels, mode, out)
           └─> out.resize(N)
```

#### 随机时间区间读取

```
用户 ReadTimeRangeMs(startMs, endMs, out)
   └─> 计算 startFrame = startMs * SampleRate / 1000
   └─> mDecoder->SeekToFrame(startFrame)
   └─> ReadFrames(endFrame - startFrame, out)
       └─> （同上：直通 / 通道下混 / 重采样路径）
```

### 3.8 关键设计决策

| 决策                              | 理由                                                                                  |
| --------------------------------- | ------------------------------------------------------------------------------------- |
| 不抛异常，全用 `Result<T>`        | 边缘 / 嵌入式场景常禁用异常；显式错误传播更安全                                        |
| 仅支持下采样                      | 项目需求只要求降采样（大 → 小），简化实现与错误处理                                    |
| 仅支持通道下混 → 1                | ASR / 单通道分析是主要场景；任意 N→M 下混需要权重矩阵，复杂度高且应用少见             |
| 通道混合不用策略接口              | 算法本质固定（Average / First / Last 三种），枚举已覆盖；引入 IChannelConverter 会过度设计 |
| 通道混合在 AudioReader 层做后处理 | 不修改 IAudioDecoder 接口，保持向后兼容；in-place 零拷贝已足够高效                    |
| 通道混合在重采样之后执行          | 重采样器按原始通道数工作；先通道下混会改变重采样器的输入通道，破坏配置约定             |
| 通道混合支持 in-place             | 输出长度 ≤ 输入长度，可在原 buffer 前 N 位置覆盖写入，省一次内存分配                  |
| miniaudio 通过 PIMPL 隐藏         | 避免把 miniaudio 的 95864 行头文件传递到用户编译单元                                  |
| `ma_data_converter` 用 `void*` 不透明指针 | miniaudio 中 `ma_data_converter` 是匿名 typedef struct，无法前向声明           |
| 文件头嗅探决定真实格式            | 与文件扩展名无关（测试音频 `.wav` 实际是 MP3）                                         |
| 字节重载不支持重采样 / 通道下混    | 简化实现；多格式输出与转换解耦，避免组合爆炸；F32 重载已覆盖主流场景                   |
| 默认解码器/重采样器延迟创建        | `AudioReader` 默认构造便宜；用户可 `SetDecoder` / `SetResampler` 替换                 |
| 错误码命名空间分区                | 1xxx / 2xxx / 3xxx / 4xxx / 5xxx / 9xxx 六大区段，便于分类与扩展                      |

### 3.9 使用示例

#### 例 1：打开文件并打印元数据

```cpp
#include "utils/audio/Audio.hpp"
using namespace utils::audio;

AudioReader reader;
auto r = reader.Open("song.mp3");
if (!r) {
    std::cerr << "Open failed: " << r.Message() << "\n";
    return 1;
}
const AudioInfo& info = reader.GetInfo();
std::cout << "format        = " << ToString(info.Format) << "\n"
          << "sample rate  = " << info.SampleRate << " Hz\n"
          << "channels     = " << info.Channels << "\n"
          << "bits/sample  = " << info.BitsPerSample << "\n"
          << "duration     = " << info.DurationMs << " ms\n"
          << "file size    = " << info.FileSizeBytes << " bytes\n"
          << "corrupted    = " << (info.IsCorrupted ? "yes" : "no") << "\n";
```

#### 例 2：流式顺序读取 PCM（F32）

```cpp
AudioReader reader;
reader.Open("song.wav");

const std::uint64_t chunkFrames = 4096;
std::vector<float> buf;
while (reader.HasMore()) {
    auto r = reader.ReadFrames(chunkFrames, buf);
    if (!r) break;
    // buf.size() == r.Value() * info.Channels
    ProcessSamples(buf.data(), buf.size());
}
```

#### 例 3：随机读取 `[1ms, 23ms)` 区间

```cpp
AudioReader reader;
reader.Open("song.mp3");
std::vector<float> samples;
auto r = reader.ReadTimeRangeMs(1, 23, samples);
// samples.size() == r.Value() * channels
// ≈ (23 - 1) * 44100 / 1000 = 970 frames
```

#### 例 4：下采样到 22050 Hz

```cpp
AudioReader reader;
reader.Open("song.mp3");
reader.SetTargetSampleRate(22050);  // 44100 -> 22050，仅支持下采样

std::vector<float> buf;
// 100ms at 22050 Hz = 2205 frames
reader.ReadFrames(2205, buf);
```

#### 例 5：注入自定义重采样器（策略模式）

```cpp
class MyResampler final : public IAudioResampler {
    // ... 实现 Configure / Process / EstimateOutFrames ...
};

AudioReader reader;
reader.Open("song.mp3");
auto custom = std::make_unique<MyResampler>();
reader.SetResampler(std::move(custom));
reader.SetTargetSampleRate(16000);
```

#### 例 6：下混到单声道（立体声 → 单声道）

适用于语音识别（ASR）、单通道频谱分析等只接受单声道输入的场景。
通道下混在解码 / 重采样之后做后处理，对原解码路径零侵入，并支持 in-place 零拷贝。

```cpp
AudioReader reader;
reader.Open("song.mp3");                          // 立体声 44100 Hz
reader.SetTargetChannels(1);                      // 启用 → 单声道
reader.SetChannelMixMode(ChannelMixMode::Average); // 默认即 Average；可选 First / Last

// 可与重采样叠加使用（重采样 → 通道下混 的顺序由库内部保证）。
// reader.SetTargetSampleRate(16000);

std::vector<float> mono;
auto r = reader.ReadFrames(4410, mono);            // mono.size() == 4410 * 1
// r.Value() == 实际读到的帧数（每帧 1 个 float）

// 关闭通道下混，恢复原始通道输出：
reader.SetTargetChannels(0);
```

三种混合策略对比（以立体声 `L, R` 为例）：

| 模式                | 输出 sample        | 适用场景                       |
| ------------------- | ------------------ | ------------------------------ |
| `ChannelMixMode::Average` | `(L + R) / 2`     | 默认；通用下混，能量均衡       |
| `ChannelMixMode::First`   | `L`               | 只关心左声道；零拷贝 stride    |
| `ChannelMixMode::Last`    | `R`               | 只关心右声道；零拷贝 stride    |

> 注：通道下混仅在 F32 重载 `ReadFrames(uint64_t, std::vector<float>&)` 上支持。
> byte 重载会拒绝带 `SetTargetChannels(1)` 的调用，返回 `InvalidArgument`。

### 3.10 错误码表

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

### 3.11 扩展指南

#### 实现自定义解码器

适用于需要支持 miniaudio 未覆盖的格式（如 AAC、Opus）或对接硬件解码器的场景。

1. 继承 `IAudioDecoder`：

   ```cpp
   class HardwareDecoder final : public IAudioDecoder {
   public:
       Result<void> Open(const std::string& filePath) override;
       bool IsOpen() const override;
       void Close() override;
       const AudioInfo& GetInfo() const override;
       Result<std::uint64_t> ReadFrames(std::uint64_t frameCount,
                                        SampleFormat outFormat,
                                        void* outBuffer) override;
       Result<void> SeekToFrame(std::uint64_t frameIndex) override;
       std::uint64_t GetPositionFrames() const override;
   private:
       AudioInfo mInfo{};
       // ... 硬件句柄等
   };
   ```

2. 注入 `AudioReader`（必须在 `Open` 之前）：

   ```cpp
   AudioReader reader;
   reader.SetDecoder(std::make_unique<HardwareDecoder>());
   reader.Open("/dev/audio0");
   ```

#### 实现自定义重采样器

适用于需要更高质量（如多相滤波器、FFT 重采样）或对接 DSP 硬件的场景。

1. 继承 `IAudioResampler`：

   ```cpp
   class PolyphaseResampler final : public IAudioResampler {
   public:
       Result<void> Configure(std::uint32_t inSampleRate,
                              std::uint32_t outSampleRate,
                              std::uint32_t channels,
                              SampleFormat format) override;
       Result<std::uint64_t> Process(const void* inBuffer,
                                     std::uint64_t inFrameCount,
                                     void* outBuffer,
                                     std::uint64_t outFrameCountCap) override;
       std::uint64_t EstimateOutFrames(std::uint64_t inFrameCount) const override;
   };
   ```

2. 注入并设置目标采样率：

   ```cpp
   AudioReader reader;
   reader.Open("song.mp3");
   reader.SetResampler(std::make_unique<PolyphaseResampler>());
   reader.SetTargetSampleRate(16000);
   ```

3. 若要禁用重采样，传入 `nullptr` 或调用 `SetTargetSampleRate(0)`：

   ```cpp
   reader.SetResampler(nullptr);     // 清除重采样器
   reader.SetTargetSampleRate(0);    // 等价：恢复原始采样率
   ```

---

## 4. DOCX 模块 utils::docx

DOCX 模板占位符替换与文档生成库：将 `.docx` 中的 `{{xx}}` 占位符替换为文本或富文本，保留原有排版格式；也可基于空白模板生成完整文档。

### 核心特性

| 功能 | 说明 |
|------|------|
| **模板占位符替换** | 将 `.docx` 模板中的 `{{xx}}` 占位符替换为文本或富文本内容，保留原有排版格式 |
| **空白模板文档生成** | 基于空白模板，传入标题+正文（纯文本/Markdown/HTML），生成完整文档 |

核心特性：
- 面向对象设计：`DocxDocument` 类管理文档生命周期（Open → 替换/生成 → Save → Close）
- 跨 Run 占位符替换：Word 将 `{{` `名称` `}}` 拆分到不同 `<w:r>` 中也能正确处理
- 格式保留：纯文本替换仅修改 `<w:t>` 文本，不改变 `<w:rPr>` 字体/字号/样式
- 富文本渲染：HTML/Markdown → GB/T 9704-2012 公文格式段落
- 双处理模式：DOM 模式（常规）与流式模式（大文件），根据内存限制自动选择
- 免费闭源商用：minizip (zlib License) + pugixml (MIT License)

### 4.1 目录结构

```
docx/
├── CMakeLists.txt                              # CMake 构建配置（C++17）
│
├── include/utils/docx/                        # ── 公开头文件（对外 API）──
│   ├── Docx.hpp                                #   umbrella
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
└── main.cpp                                    # test_docx CLI 工具
```

### 4.2 分层设计

| 层次 | 文件 | 职责 |
|------|------|------|
| **公开 API 层** | `docx_document.h`, `rich_content.h` | 对外接口声明，调用方只需包含这两个头文件 |
| **业务实现层** | `docx_document.cpp`, `rich_content.cpp` | 占位符查找/替换、文档生成、富文本解析渲染 |
| **流式处理层** | `streaming_processor.h/.cpp` | 大文件按段落逐段处理，内存占用 O(最大段落) |
| **工具层** | `zip_utils.*`, `xml_utils.*` | minizip/pugixml 封装，提供底层 I/O 能力 |

### 4.3 核心类 DocxDocument

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

    // 禁拷贝（持有临时目录等资源）
    DocxDocument(const DocxDocument&) = delete;
    DocxDocument& operator=(const DocxDocument&) = delete;
    // 允许移动
    DocxDocument(DocxDocument&&) = default;
    DocxDocument& operator=(DocxDocument&&) = default;

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

**设计要点**：
- **Open → 操作 → Save → Close** 生命周期模型，打开一次可多次操作后一次保存
- **RAII 资源管理**：析构函数自动调用 `Close()` 清理临时目录
- **禁拷贝允移动**：持有临时目录等资源不可拷贝，但可移动转移所有权
- **配置集中**：`DocxConfig` 统一管理内存限制、临时目录、日志等参数

### 4.4 双处理模式

内部根据 `文件大小 + 替换内容大小` 与 `memoryLimit` 比较，自动选择处理模式：

| 模式 | 触发条件 | 内存占用 | 实现方式 |
|------|---------|---------|---------|
| **DOM 模式** | 总大小 ≤ `memoryLimit` | O(文件大小) | pugixml 全量加载 `document.xml` 到内存，支持多次替换复用 DOM |
| **流式模式** | 总大小 > `memoryLimit` | O(最大段落大小) | 64KB 块读取，按 `<w:p>` 边界逐段处理，段落外内容直接透传 |

两种模式均支持表格单元格内的占位符替换（流式模式扫描所有 `<w:p>` 标签，不区分位置）。

```
用户调用 ReplaceText/ReplaceRich
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
 ReplaceTextDom_()  ReplaceTextStreaming_()
 遍历 <w:p> 逐段替换   逐段读取+替换+写入
    │         │
    └────┬────┘
         ▼
   saveXmlDom_() / 流式输出
         │
         ▼
     zip_() 压缩
```

### 4.5 占位符替换实现

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

### 4.6 富文本渲染实现

HTML/Markdown 替换流程：

```
输入字符串 (HTML/Markdown)
         │
         ▼
  ParseHtml() / ParseMarkdown()
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

### 4.7 文档生成实现

`GenerateDocument` 从空白模板生成文档：
1. 打开空白模板（`<w:body>` 中仅有 `<w:sectPr>`，无段落）
2. 若提供标题，创建 h1 段落（方正小标宋简体 22pt 居中）
3. 根据内容类型解析正文：
   - Plain：按 `\n` 分割，每行生成仿宋正文段落
   - HTML/Markdown：复用富文本解析器生成格式化段落
4. 将所有段落插入到 `<w:sectPr>` 之前

### 4.8 API 接口文档

#### 配置：DocxConfig

```cpp
struct DocxConfig {
    size_t memoryLimit = 10 * 1024 * 1024;  // 内存限制（字节），默认 10MB
    std::string tempDir;                     // 临时解压目录，空则使用系统临时目录
    bool verbose = false;                    // 是否输出详细日志
    bool keepTempDir = false;                // 是否保留临时目录（调试用）
};
```

#### 生命周期管理

```cpp
Result<void> Open(const std::string& path);    // 打开 docx，解压到临时目录
Result<void> Save(const std::string& path);    // 保存为 docx
void Close();                                // 关闭并清理临时目录
```

#### 文本替换：ReplaceText

```cpp
// 批量替换：传入 map<string,string>
Result<ReplaceStats> ReplaceText(const std::map<std::string, std::string>& replacements);

// 单模式替换
Result<ReplaceStats> ReplaceText(const std::string& pattern, const std::string& replacement);
```

| 参数 | 说明 |
|------|------|
| `replacements` | key 为占位符名称（不含 `{{}}`），value 为替换文本；key 为 `"*"` 通配所有 |
| `pattern` | `"{{*}}"` 匹配所有占位符；`"{{会议主题}}"` 精确匹配 |
| `replacement` | 替换文本 |

**行为**：遍历所有 `<w:p>` 段落（含表格单元格），查找 `{{key}}` 替换为 value，保留原有 `<w:rPr>` 格式。

#### 富文本替换：ReplaceRich

```cpp
Result<ReplaceStats> ReplaceRich(const std::map<std::string, RichReplacement>& replacements);
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

#### 文档生成：GenerateDocument

```cpp
Result<ReplaceStats> GenerateDocument(const std::string& title,
                                const std::string& bodyContent,
                                ContentType contentType);
```

| 参数 | 说明 |
|------|------|
| `title` | 标题（可选，为空则不添加标题）。以 h1 格式渲染：方正小标宋简体 22pt 居中 |
| `bodyContent` | 正文内容 |
| `contentType` | 内容类型：`Plain` / `HTML` / `Markdown` |

**行为**：在空白模板的 `<w:body>` 中插入标题和正文段落。需先调用 `Open()` 打开空白模板。

#### 状态查询

```cpp
bool IsOpen() const;           // 文档是否已打开
bool IsStreamingMode() const;  // 是否处于流式处理模式
size_t GetFileSize() const;    // 获取已打开文件大小（字节）
const DocxConfig& Config() const;  // 获取配置
```

#### 富文本模块独立使用

```cpp
#include "utils/docx/rich_content.h"

// 解析 Markdown/HTML 为结构化段落
auto paragraphs = utils::docx::ParseMarkdown("# 标题\n正文");
auto paragraphs = utils::docx::ParseHtml("<h2>标题</h2><p>正文</p>");

// 渲染到已有 XML 文档（DOM 模式）
utils::docx::RenderParagraphsToXml(parentNode, paragraphs, placeholderNode);

// 序列化为 XML 字符串（流式模式）
std::string xml = utils::docx::SerializeParagraphs(paragraphs);

// 插入到指定节点之前（文档生成）
utils::docx::AppendParagraphsBefore(parentNode, paragraphs, sectPrNode);
```

#### ZIP 工具函数

独立的 ZIP 压缩/打包/解压函数，返回 `Result<void>` 复用错误码体系，不依赖 `DocxDocument`：

```cpp
#include "utils/docx/Docx.hpp"

// 压缩：目录或文件 → ZIP（Deflate 压缩）
utils::docx::Result<void> ZipCompress(const std::string& srcPath,
                        const std::string& outputPath,
                        const std::string& zipName);

// 打包：目录或文件 → ZIP（仅存储不压缩，速度优先）
utils::docx::Result<void> ZipStore(const std::string& srcPath,
                       const std::string& outputPath,
                       const std::string& zipName);

// 解压：ZIP → 目录
utils::docx::Result<void> ZipExtract(const std::string& zipPath,
                        const std::string& destDir);
```

| 函数 | 参数 | 说明 |
|------|------|------|
| `ZipCompress` | `srcPath` | 源路径（目录或文件均可） |
| | `outputPath` | 输出目录路径（不存在则自动创建） |
| | `zipName` | ZIP 包名（如 `"archive.zip"`） |
| `ZipStore` | `srcPath` | 源路径（目录或文件均可） |
| | `outputPath` | 输出目录路径（不存在则自动创建） |
| | `zipName` | ZIP 包名（如 `"archive.zip"`） |
| `ZipExtract` | `zipPath` | ZIP 文件路径 |
| | `destDir` | 解压目标目录（不存在则自动创建） |

**行为**：
- `srcPath` 为目录时，递归打包其下所有文件，保持目录结构
- `srcPath` 为文件时，打包单个文件（ZIP 内仅含文件名）
- 输出的 ZIP 使用正斜杠 `/` 路径分隔符，Windows/Mac/Linux 均可原生解压
- `ZipCompress` 使用 `Z_DEFLATED` 标准压缩算法，与系统自带工具完全兼容
- `ZipStore` 使用 Store 方法（method=0，不压缩），仅打包数据，速度更快；适用于数据本身已压缩（如 docx 内部 XML 已 deflate）或对速度要求高的场景

### 4.9 使用示例

#### 例 1：纯文本批量替换

```cpp
#include "utils/docx/Docx.hpp"

int main() {
    std::map<std::string, std::string> replacements = {
        {"会议主题", "年终总结会议"},
        {"会议时间", "2024年12月25日"},
        {"主持人", "张三"},
    };

    utils::docx::DocxDocument doc;
    if (!doc.Open("template.docx").IsOk()) return 1;

    auto result = doc.ReplaceText(replacements);
    if (result.IsOk()) {
        printf("替换 %d 处\n", result.Value().totalReplaced);
        doc.Save("output.docx");
    }

    doc.Close();
    return result.IsOk() ? 0 : 1;
}
```

#### 例 2：通配符替换

```cpp
utils::docx::DocxDocument doc;
doc.Open("template.docx");

// 所有 {{xx}} 替换为同一文本
doc.ReplaceText("{{*}}", "小红有才");
doc.Save("output.docx");
doc.Close();
```

#### 例 3：富文本混合替换

```cpp
#include "utils/docx/Docx.hpp"
#include "utils/docx/rich_content.h"

int main() {
    std::map<std::string, utils::docx::RichReplacement> replacements;

    // 纯文本替换（保留原格式）
    replacements["标题"] = {"2024年度工作报告", utils::docx::ContentType::Plain};

    // Markdown 替换（生成格式化段落）
    replacements["正文"] = {
        "## 一、工作总结\n\n"
        "本年度完成主要工作：\n\n"
        "1. 系统架构升级\n"
        "2. 性能优化\n\n"
        "## 二、下年度计划\n\n"
        "继续推进**数字化转型**。",
        utils::docx::ContentType::Markdown
    };

    // HTML 替换
    replacements["备注"] = {
        "<h3>注意事项</h3>"
        "<p>请各部门提前准备<b>汇报材料</b>。</p>"
        "<ul><li>材料需含<i>数据图表</i></li></ul>",
        utils::docx::ContentType::HTML
    };

    utils::docx::DocxDocument doc;
    doc.Open("template.docx");
    auto result = doc.ReplaceRich(replacements);
    if (result.IsOk()) doc.Save("output.docx");
    doc.Close();
    return result.IsOk() ? 0 : 1;
}
```

#### 例 4：从空白模板生成文档

```cpp
#include "utils/docx/Docx.hpp"

int main() {
    utils::docx::DocxDocument doc;
    doc.Open("templates/默认模板.docx");

    // 传入标题 + Markdown 正文
    std::string mdContent = "## 会议内容\n\n"
                            "本次讨论了以下事项：\n\n"
                            "- 项目进度\n"
                            "- 预算审批\n";

    doc.GenerateDocument("季度工作总结会议纪要", mdContent,
                          utils::docx::ContentType::Markdown);
    doc.Save("output.docx");
    doc.Close();
    return 0;
}
```

#### 例 5：自定义配置

```cpp
utils::docx::DocxConfig config;
config.tempDir = "/my/custom/temp";    // 自定义临时目录（SSD 加速）
config.keepTempDir = true;              // 保留临时文件用于调试
config.verbose = true;                  // 输出详细日志
config.memoryLimit = 5 * 1024 * 1024;  // 5MB 内存限制

utils::docx::DocxDocument doc(config);
doc.Open("input.docx");
doc.ReplaceText("{{*}}", "替换文本");
doc.Save("output.docx");
doc.Close();

// 调试：检查 /my/custom/temp/word/document.xml
```

#### 例 6：错误处理

```cpp
utils::docx::DocxDocument doc;

auto openRes = doc.Open("input.docx");
if (!openRes.IsOk()) {
    switch (openRes.Code()) {
        case static_cast<int>(utils::docx::ErrorCode::FileNotFound):
            std::cerr << "文件不存在: " << openRes.Message() << std::endl;
            break;
        case static_cast<int>(utils::docx::ErrorCode::UnzipFailed):
            std::cerr << "文件可能损坏" << std::endl;
            break;
        default:
            std::cerr << utils::docx::ToMessage(openRes.Code()) << std::endl;
    }
    return 1;
}

auto result = doc.ReplaceText("{{*}}", "替换文本");
if (!result.IsOk()) {
    std::cerr << utils::docx::ToMessage(result.Code()) << std::endl;
    doc.Close();
    return 1;
}

doc.Save("output.docx");
doc.Close();
```

#### 例 7：ZIP 工具：压缩/打包/解压

```cpp
#include "utils/docx/Docx.hpp"

// 压缩目录（Deflate 压缩）
auto err = utils::docx::ZipCompress(
    "/home/user/documents",    // 源目录
    "/home/user/output",       // 输出目录（自动创建）
    "backup.zip");              // 包名
if (!err.IsOk()) {
    std::cerr << err.Message() << std::endl;
}

// 打包目录（仅存储不压缩，速度优先）
auto errStore = utils::docx::ZipStore(
    "/home/user/documents",    // 源目录
    "/home/user/output",       // 输出目录（自动创建）
    "backup_store.zip");        // 包名

// 压缩单个文件
auto err2 = utils::docx::ZipCompress(
    "/home/user/report.pdf",   // 源文件
    "/home/user/output",       // 输出目录
    "report.zip");              // 包名

// 解压
auto err3 = utils::docx::ZipExtract(
    "/home/user/output/backup.zip",  // ZIP 文件
    "/home/user/restored");           // 解压目录（自动创建）
if (!err3.IsOk()) {
    std::cerr << err3.Message() << std::endl;
}
```

### 4.10 命令行工具

构建后生成 `test_docx` CLI 工具：

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

### 4.11 格式标准

#### GB/T 9704-2012 公文格式映射

| 元素 | 字体 | 字号 | 对齐 | 缩进 | 行距 |
|------|------|------|------|------|------|
| h1 | 方正小标宋简体 | 22pt | 居中 | 无 | 1.0 |
| h2 | 黑体 | 16pt | 左对齐 | 无 | 1.0 |
| h3 | 楷体 | 16pt | 左对齐 | 无 | 1.0 |
| h4 / 正文 | 仿宋 | 16pt | 两端对齐 | 首行缩进 32pt | 1.0 |
| 列表 | 仿宋 | 16pt | 左对齐 | 悬挂缩进 | 1.0 |
| 西文 | Times New Roman | 同字号 | - | - | - |

#### OOXML 单位换算

| 单位 | 换算 | 示例 |
|------|------|------|
| 字体大小 | 1pt = 2 half-points | 16pt → `<w:sz w:val="32"/>` |
| 缩进 | 1pt = 20 twips | 32pt → `<w:ind w:firstLine="640"/>` |
| 行距 | 240 = 1.0 倍 | 1.0 → `<w:spacing w:line="240"/>` |

#### 支持的 Markdown 语法

| 语法 | 示例 | 渲染效果 |
|------|------|---------|
| 标题 | `# h1` ~ `#### h4` | h1~h4 |
| 加粗 | `**text**` | **加粗** |
| 斜体 | `*text*` | *斜体* |
| 无序列表 | `- item` 或 `* item` | 列表项 |
| 有序列表 | `1. item` | 有序列表 |
| 段落 | 空行分隔 | 正文段落 |

#### 支持的 HTML 标签

| 标签 | 渲染效果 |
|------|---------|
| `<h1>` ~ `<h4>` | h1~h4 |
| `<p>` | 正文段落 |
| `<b>` / `<strong>` | 加粗 |
| `<i>` / `<em>` | 斜体 |
| `<u>` | 下划线 |
| `<ul>` / `<ol>` / `<li>` | 无序/有序列表 |
| `<br>` | 换行 |

### 4.12 错误码表

按域分区：1xxx=文件/IO，2xxx=XML/处理（与 audio 域保持一致的分区风格）：

| Code | 枚举值 | 含义 | 常见原因 |
|------|--------|------|---------|
| 0 | `Ok` | 成功 | - |
| 1001 | `FileNotFound` | 输入文件不存在 | 路径错误 |
| 1002 | `FileNotReadable` | 文件不可读 | 权限不足 |
| 1003 | `InvalidDocxFormat` | 非合法 docx | 文件损坏 |
| 1004 | `UnzipFailed` | 解压失败 | zip 结构损坏 |
| 1005 | `TempDirCreateFailed` | 临时目录创建失败 | `/tmp` 不可写 |
| 1006 | `OutputWriteFailed` | 输出文件写入失败 | 路径不可写 |
| 2001 | `XmlParseFailed` | XML 解析失败 | document.xml 格式错误 |
| 2002 | `XmlSaveFailed` | XML 保存失败 | 磁盘空间不足 |
| 2003 | `ZipFailed` | 压缩失败 | 磁盘空间不足 |
| 2004 | `NoMatchFound` | 未找到匹配的占位符 | 模板无对应 `{{xx}}` |
| 2005 | `InvalidPattern` | 占位符模式非法 | 替换映射为空 / 正文为空 |
| 2006 | `NotOpened` | 文档未打开 | 未调用 `Open()` 或已 `Close()` |
| 2099 | `UnknownError` | 未知错误 | - |

可通过 `utils::docx::ToMessage(code)` 获取简短英文描述（与枚举名一致）。

### 4.13 限制与注意事项

1. **仅处理 `word/document.xml`**：不处理页眉/页脚/尾注/批注中的占位符
2. **富文本仅替换独占段落**：HTML/Markdown 替换只对占位符独占一行的段落生效（段落全文仅 `{{xx}}`）
3. **不支持嵌套占位符**：`{{outer{{inner}}content}}` 无法正确处理
4. **不支持图片/表格**：富文本不支持插入图片或表格，仅支持段落级别内容
5. **字形依赖**：GB/T 9704-2012 标准字体（方正小标宋简体、黑体、楷体、仿宋）需在目标系统安装
6. **GenerateDocument 始终使用 DOM 模式**：空白模板很小，无需流式处理
7. **ZIP 跨平台兼容**：`ZipCompress` 生成的 ZIP 文件使用正斜杠路径分隔符和 `Z_DEFLATED` 标准压缩，Windows/Mac/Linux 系统自带工具均可直接解压

---

## 5. 合并对照表

| 项 | 原 audio_resover | 原 docx_temp_helper | 合并后 |
|----|------------------|---------------------|--------|
| 命名空间 | `audio_resover` | `docx_temp_helper` | `utils::audio` / `utils::docx` |
| 错误模型 | 自有 `Result<T>` | `ErrorInfo` + `ReplaceResult` | 统一 `utils::Result<T>`(common 模块) |
| 目录名 | `audio_resover`(冗长) | `docx_temp_helper`(冗长) | `audio` / `docx` |
| 三方库 | 各自 third_party | 各自 third_party | 集中 `utils/third_party/` |
| 构建选项 | 各自 CMake | 各自 CMake | 顶层统一 `UTILS_BUILD_TESTS` 等 |
| 测试断言 | 自研断言宏 | 自研断言宏 | 保留自研断言宏 |
| umbrella 头 | `AudioResover.hpp` | 无统一 umbrella | `Audio.hpp` / `Docx.hpp` |

---

## 6. 命名规范

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
