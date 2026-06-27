# audio_resover

一个面向边缘设备的 C++17 音频处理库：支持主流格式解码、元数据查询、流式/随机 PCM 读取、可替换的下采样算法，全部接口返回 `Result<T>` 而非抛异常，适合禁用异常的运行时。

基于 [miniaudio](https://github.com/mackron/miniaudio) v0.11.25（MIT，单头文件，零依赖，可闭源商用）。

---

## 目录

- [核心特性](#核心特性)
- [依赖](#依赖)
- [目录结构](#目录结构)
- [结构设计与类接口说明](#结构设计与类接口说明)
- [快速开始](#快速开始)
- [使用示例](#使用示例)
- [公共 API 参考](#公共-api-参考)
- [错误码表](#错误码表)
- [命名规范](#命名规范)
- [扩展指南](#扩展指南)
- [License](#license)

---

## 核心特性

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
| 7   | CMake 工程                               | 标准 `target_include_directories`、BUILD_INTERFACE/INSTALL_INTERFACE、可独立构建      |

---

## 依赖

| 名称       | 版本    | License | 用途                  | 安装方式                                    |
| ---------- | ------- | ------- | --------------------- | ------------------------------------------- |
| miniaudio  | 0.11.25 | MIT     | 解码 / 重采样 / 格式转换 | 已 vendored 在 `third_party/miniaudio/`    |
| CMake      | ≥ 3.16  | BSD-3   | 构建系统              | 系统自带                                    |
| C++ 编译器 | ≥ C++17 | -       | 编译                  | g++ / clang++                               |

**无需任何 `apt install`**，miniaudio 是单头文件零依赖库。在 Linux 上仅需链接 `pthread` 与 `m`，CMake 已自动处理。

---

## 目录结构

```
audio_resover/
├── CMakeLists.txt              # 顶层 CMake 配置（构建 libaudio_resover.a）
├── README.md                   # 本文件
├── include/
│   └── audio_resover/
│       ├── AudioResover.hpp    # umbrella 头文件（include 这一个即可）
│       ├── AudioReader.hpp     # 高层 facade（用户主入口）
│       ├── AudioChannelConverter.hpp # 通道混合策略枚举
│       ├── AudioInfo.hpp       # 元数据 POD
│       ├── AudioError.hpp     # 错误码 + Result<T>
│       ├── AudioFormat.hpp    # SampleFormat / ContainerFormat 枚举
│       ├── IAudioDecoder.hpp  # 解码器策略接口（用于扩展）
│       └── IAudioResampler.hpp # 重采样策略接口（用于扩展）
├── src/
│   ├── AudioReader.cpp         # facade 实现
│   ├── ChannelMixer.{hpp,cpp}  # 通道混合器（私有工具，F32 交错布局）
│   ├── MiniaudioDecoder.{hpp,cpp}  # 默认 IAudioDecoder 实现（私有头）
│   ├── MiniaudioResampler.{hpp,cpp}# 默认 IAudioResampler 实现（私有头）
│   ├── MiniaudioGuard.hpp      # miniaudio 初始化 RAII 钩子（预留）
│   └── miniaudio_impl.cpp      # 唯一定义 MINIAUDIO_IMPLEMENTATION 的 TU
├── tests/
│   ├── CMakeLists.txt
│   └── test_audio_reader.cpp   # 12 个测试用例，自研断言宏（无 GoogleTest）
└── third_party/
    └── miniaudio/
        ├── miniaudio.h         # v0.11.25 单头文件
        └── LICENSE             # MIT
```

---

## 结构设计与类接口说明

### 整体架构

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

### 设计模式

| 模式        | 应用位置                                          | 目的                                       |
| ----------- | ------------------------------------------------- | ------------------------------------------ |
| Facade      | `AudioReader`                                     | 为外部提供统一简化入口，隔离策略切换细节   |
| Strategy    | `IAudioDecoder` / `IAudioResampler`               | 解码器 / 重采样器可替换，降低耦合          |
| PIMPL       | `MiniaudioDecoder` / `MiniaudioResampler`         | 隐藏 miniaudio 私有依赖，缩短编译时间       |
| Value-like  | `Result<T>`                                       | 用值类型承载错误，避免异常                 |
| RAII        | `MiniaudioGuard`（预留）                          | 全局资源获取 / 释放（当前为空操作）        |
| 内部工具类  | `ChannelMixer`（私有静态方法）                    | 通道混合逻辑不暴露为接口；算法固定，三种模式已枚举覆盖 |

### 模块依赖关系

```
include/audio_resover/AudioResover.hpp     <- umbrella，用户唯一入口
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

### 公开类型一览

#### 枚举与 POD

```cpp
namespace audio_resover {

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

} // namespace audio_resover
```

#### 错误码与 Result

```cpp
enum class AudioErrorCode : int {
    Ok = 0,
    FileNotFound = 1001, FileOpenFailed, FileReadFailed,
    UnknownFormat = 2001, UnsupportedFormat, CorruptedFile, DecodeFailed, SeekFailed,
    InvalidArgument = 3001, OutOfRange, NotOpened, AlreadyOpened,
    UnsupportedResampleDirection = 4001, ResamplerNotSet, ResamplerInitFailed, ResampleFailed,
    UnsupportedChannelDirection = 5001, ChannelMixFailed,
    InternalError = 9001, NotImplemented,
};

template <typename T> class Result {
public:
    bool IsOk() const;
    explicit operator bool() const;
    const T& Value() const;                // 仅 IsOk() 时合法
    AudioErrorCode Code() const;
    const std::string& Message() const;
    static Result Ok(T value);
    static Result Fail(AudioErrorCode code, std::string msg = "");
};
// Result<void> 特化：不含 Value()。
```

#### AudioReader（Facade 主入口）

```cpp
class AudioReader {
public:
    // 0. 打开 / 关闭
    Result<void> Open(const std::string& filePath);
    Result<void> SetDecoder(std::unique_ptr<IAudioDecoder>);  // Open 之前替换
    bool IsOpen() const;
    void Close();

    // 1. 元数据
    const AudioInfo& GetInfo() const;

    // 2. 读取（F32 流式 / 字节流 / 时间区间）
    Result<uint64_t> ReadFrames(uint64_t frameCount, std::vector<float>& out);
    Result<uint64_t> ReadFrames(uint64_t frameCount, SampleFormat fmt,
                                std::vector<uint8_t>& out);
    Result<uint64_t> ReadTimeRangeMs(uint64_t startMs, uint64_t endMs,
                                     std::vector<float>& out);

    // 位置 / 跳转
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

契约：

- 仅下采样（`outSampleRate <= inSampleRate`）
- 采样格式与通道数保持不变
- `Process` 可增量调用，内部状态跨调用保留

### 数据流

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

### 关键设计决策

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

---

## 快速开始

### 构建

```bash
cd audio_resover
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

产物：

- `build/libaudio_resover.a` —— 静态库
- `build/tests/test_audio_reader` —— 测试可执行文件

### 选项

| CMake 选项                          | 默认 | 说明                       |
| ----------------------------------- | ---- | -------------------------- |
| `AUDIO_RESOVER_BUILD_TESTS`         | ON   | 构建测试程序               |
| `AUDIO_RESOVER_WARNINGS_AS_ERRORS`  | OFF  | 把警告当作错误             |
| `AUDIO_TEST_FILE`                   | 自动 | 测试默认音频路径（可覆盖） |

### 运行测试

```bash
# 使用编译期默认路径（指向 ../一小时空音频.wav）
./build/tests/test_audio_reader

# 或运行时指定音频文件
./build/tests/test_audio_reader /path/to/your/audio.mp3
```

预期输出（10 个用例全通过）：

```
audio_resover test suite
Test file: /home/hong/code/learn_cpp/一小时空音频.wav
...
checks: 690  failures: 0
```

---

## 使用示例

只需包含一个 umbrella 头文件并链接 `audio_resover` 目标：

```cmake
target_link_libraries(your_app PRIVATE audio_resover::audio_resover)
```

```cpp
#include "audio_resover/AudioResover.hpp"
using namespace audio_resover;
```

### 例 1：打开文件并打印元数据

```cpp
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

### 例 2：流式顺序读取 PCM（F32）

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

### 例 3：随机读取 `[1ms, 23ms)` 区间

```cpp
AudioReader reader;
reader.Open("song.mp3");
std::vector<float> samples;
auto r = reader.ReadTimeRangeMs(1, 23, samples);
// samples.size() == r.Value() * channels
// ≈ (23 - 1) * 44100 / 1000 = 970 frames
```

### 例 4：下采样到 22050 Hz

```cpp
AudioReader reader;
reader.Open("song.mp3");
reader.SetTargetSampleRate(22050);  // 44100 -> 22050，仅支持下采样

std::vector<float> buf;
// 100ms at 22050 Hz = 2205 frames
reader.ReadFrames(2205, buf);
```

### 例 5：注入自定义重采样器（策略模式）

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

### 例 6：错误处理（Result\<T> + AudioErrorCode）

```cpp
AudioReader reader;
auto openRes = reader.Open("/tmp/nonexistent.mp3");
if (!openRes) {
    switch (openRes.Code()) {
        case AudioErrorCode::FileNotFound:
            std::cerr << "文件不存在\n"; break;
        case AudioErrorCode::UnknownFormat:
            std::cerr << "格式无法识别\n"; break;
        default:
            std::cerr << "其它错误: " << ToMessage(openRes.Code()) << "\n";
    }
}
```

### 例 7：下混到单声道（立体声 → 单声道）

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

---

## 公共 API 参考

### `AudioReader` —— Facade（用户主入口）

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

### `AudioInfo` —— 元数据 POD

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

### `IAudioDecoder` —— 解码器策略接口

```cpp
class IAudioDecoder {
public:
    virtual ~IAudioDecoder() = default;
    virtual Result<void> Open(const std::string& filePath) = 0;
    virtual bool IsOpen() const = 0;
    virtual void Close() = 0;
    virtual const AudioInfo& GetInfo() const = 0;
    virtual Result<std::uint64_t> ReadFrames(std::uint64_t frameCount,
                                             SampleFormat outFormat,
                                             void* outBuffer) = 0;
    virtual Result<void> SeekToFrame(std::uint64_t frameIndex) = 0;
    virtual std::uint64_t GetPositionFrames() const = 0;
};
```

### `IAudioResampler` —— 重采样策略接口

```cpp
class IAudioResampler {
public:
    virtual ~IAudioResampler() = default;
    virtual Result<void> Configure(std::uint32_t inSampleRate,
                                   std::uint32_t outSampleRate,
                                   std::uint32_t channels,
                                   SampleFormat format) = 0;
    virtual Result<std::uint64_t> Process(const void* inBuffer,
                                          std::uint64_t inFrameCount,
                                          void* outBuffer,
                                          std::uint64_t outFrameCountCap) = 0;
    virtual std::uint64_t EstimateOutFrames(std::uint64_t inFrameCount) const = 0;
};
```

**契约**：仅支持下采样（`outSampleRate <= inSampleRate`）；采样格式与通道数保持不变；`Process` 可增量调用，内部状态跨调用保留。

### `Result<T>` —— 替代异常的返回类型

```cpp
template <typename T>
class Result {
public:
    bool IsOk() const;
    explicit operator bool() const;        // 隐式转 bool，便于 if (!r)
    const T& Value() const;                // 仅 IsOk() 时合法
    AudioErrorCode Code() const;
    const std::string& Message() const;
    static Result Ok(T value);
    static Result Fail(AudioErrorCode code, std::string msg = "");
};

template <>
class Result<void> { /* 仅含 Code() / Message()，无 Value() */ };
```

### `SampleFormat` / `ContainerFormat` 枚举

```cpp
enum class SampleFormat : int {
    Unknown, U8, S16, S24, S32, F32
};

enum class ContainerFormat : int {
    Unknown, Wav, Mp3, Flac, Vorbis, Raw
};

// 友好打印
const char* ToString(SampleFormat);
const char* ToString(ContainerFormat);
```

---

## 错误码表

来自 [AudioError.hpp](include/audio_resover/AudioError.hpp)。错误码按命名空间分区：

| Code | 枚举值                          | 含义                                |
| ---- | -------------------------------- | ----------------------------------- |
| 0    | `Ok`                             | 成功                                |
| 1001 | `FileNotFound`                   | 文件不存在                          |
| 1002 | `FileOpenFailed`                 | 打开文件失败                        |
| 1003 | `FileReadFailed`                 | 读文件失败                          |
| 2001 | `UnknownFormat`                  | 无法识别的音频格式                  |
| 2002 | `UnsupportedFormat`             | 不支持的格式                        |
| 2003 | `CorruptedFile`                  | 文件已损坏                          |
| 2004 | `DecodeFailed`                  | 解码失败                            |
| 2005 | `SeekFailed`                    | 跳转失败                            |
| 3001 | `InvalidArgument`               | 参数非法                            |
| 3002 | `OutOfRange`                    | 位置超出范围                        |
| 3003 | `NotOpened`                     | 尚未调用 `Open`                     |
| 3004 | `AlreadyOpened`                 | 已经打开                            |
| 4001 | `UnsupportedResampleDirection`  | 仅支持下采样（不允许上采样）         |
| 4002 | `ResamplerNotSet`               | 未配置重采样器                      |
| 4003 | `ResamplerInitFailed`           | 重采样器初始化失败                  |
| 4004 | `ResampleFailed`                | 重采样失败                          |
| 5001 | `UnsupportedChannelDirection`   | 不支持上混（target > source）       |
| 5002 | `ChannelMixFailed`              | 通道混合失败                        |
| 9001 | `InternalError`                 | 内部错误                            |
| 9002 | `NotImplemented`                | 未实现                              |

可通过 `ToMessage(AudioErrorCode)` 获取简短描述。

---

## 命名规范

| 类别           | 规则             | 示例                              |
| -------------- | ---------------- | --------------------------------- |
| 函数 / 方法    | 大驼峰 (PascalCase) | `ReadFrames` / `SeekToMs`         |
| 局部变量 / 参数 | 小驼峰 (camelCase)  | `frameCount` / `filePath`         |
| 成员变量       | `m` + 大驼峰       | `mDecoder` / `mTargetSampleRate`   |
| 类型 / 类      | 大驼峰 (PascalCase) | `AudioReader` / `IAudioDecoder`   |
| 枚举值         | 大驼峰 (PascalCase) | `SampleFormat::F32`               |
| 命名空间       | 小写              | `audio_resover`                    |
| 文件名         | 大驼峰            | `AudioReader.hpp`                  |

代码风格由项目根目录的 `.clang-format` 配置约束（基于 LLVM，4 空格 tab、Linux 大括号、100 列）。

---

## 扩展指南

### 实现自定义解码器

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

### 实现自定义重采样器

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

## License

本库源码采用 **MIT License**，与所依赖的 miniaudio (MIT) 兼容，可闭源商用。

- 本库源码：见仓库根 LICENSE 文件（如未单独提供，默认 MIT）
- miniaudio：`third_party/miniaudio/LICENSE`（MIT，Copyright (c) David Reid）
