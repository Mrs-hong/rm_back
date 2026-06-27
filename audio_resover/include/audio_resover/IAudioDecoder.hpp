// audio_resover - C++17 音频处理库
// 公开头文件：解码器策略接口。
//
// IAudioDecoder 抽象了“打开音频文件 / 查询元数据 / 读取 PCM 帧”这一组
// 与编解码器强相关的操作。默认实现 MiniaudioDecoder 通过 miniaudio 覆盖
// WAV/MP3/FLAC/Vorbis；用户可自行实现本接口以接入其它后端
// （例如 AAC、Opus，或硬件解码器）。
#pragma once

#include <cstdint>
#include <string>

#include "audio_resover/AudioError.hpp"
#include "audio_resover/AudioFormat.hpp"
#include "audio_resover/AudioInfo.hpp"

namespace audio_resover
{

class IAudioDecoder
{
  public:
	virtual ~IAudioDecoder() = default;

	// 按路径打开文件。成功后 GetInfo() 返回有效元数据。
	// 若文件完全无法解析，返回 UnknownFormat / CorruptedFile。
	virtual Result<void> Open(const std::string& filePath) = 0;

	virtual bool IsOpen() const = 0;
	virtual void Close() = 0;

	// 元数据。仅在 Open() 成功后有效。
	virtual const AudioInfo& GetInfo() const = 0;

	// 顺序读取最多 frameCount 个 PCM 帧到 outBuffer。
	// outFormat 决定 outBuffer 中的采样布局；基于 miniaudio 的实现会自动
	// 完成采样格式转换。
	// 返回实际读取的帧数（EOF 时可能少于请求）；EOF 时返回 Ok 且值为 0。
	virtual Result<std::uint64_t> ReadFrames(std::uint64_t frameCount, SampleFormat outFormat,
											 void* outBuffer) = 0;

	// 把读指针跳转到绝对 PCM 帧索引。
	virtual Result<void> SeekToFrame(std::uint64_t frameIndex) = 0;

	// 当前读指针位置（以 PCM 帧为单位）。
	virtual std::uint64_t GetPositionFrames() const = 0;
};

} // namespace audio_resover
