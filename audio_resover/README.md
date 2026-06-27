# audio_resover

一个面向边缘设备的 C++17 音频处理库：支持主流格式解码、元数据查询、流式/随机 PCM 读取、可替换的下采样算法，全部接口返回 `Result<T>` 而非抛异常，适合禁用异常的运行时。

基于 [miniaudio](https://github.com/mackron/miniaudio) v0.11.25（MIT，单头文件，零依赖，可闭源商用）。

---

## 目录

- [核心特性](#核心特性)
- [依赖](#依赖)
- [目录结构](#目录结构)
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
| 4   | 错误码                                   | 18 个细分错误码，按命名空间分区（1xxx 文件 / 2xxx 解码 / 3xxx 参数 / 4xxx 重采样 / 9xxx 内部） |
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
│       ├── AudioInfo.hpp       # 元数据 POD
│       ├── AudioError.hpp     # 错误码 + Result<T>
│       ├── AudioFormat.hpp    # SampleFormat / ContainerFormat 枚举
│       ├── IAudioDecoder.hpp  # 解码器策略接口（用于扩展）
│       └── IAudioResampler.hpp # 重采样策略接口（用于扩展）
├── src/
│   ├── AudioReader.cpp         # facade 实现
│   ├── MiniaudioDecoder.{hpp,cpp}  # 默认 IAudioDecoder 实现（私有头）
│   ├── MiniaudioResampler.{hpp,cpp}# 默认 IAudioResampler 实现（私有头）
│   ├── MiniaudioGuard.hpp      # miniaudio 初始化 RAII 钩子（预留）
│   └── miniaudio_impl.cpp      # 唯一定义 MINIAUDIO_IMPLEMENTATION 的 TU
├── tests/
│   ├── CMakeLists.txt
│   └── test_audio_reader.cpp   # 10 个测试用例，自研断言宏（无 GoogleTest）
└── third_party/
    └── miniaudio/
        ├── miniaudio.h         # v0.11.25 单头文件
        └── LICENSE             # MIT
```

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
