// audio_resover - C++17 音频处理库
// 公开头文件：AudioReader 高层 facade（外观模式）。
//
// AudioReader 是用户主入口。它组合了一个 IAudioDecoder
// （默认：MiniaudioDecoder）与可选的 IAudioResampler（默认：MiniaudioResampler），
// 并提供帧域与毫秒域的便捷 API。
//
// 典型用法：
//   AudioReader reader;
//   auto r = reader.Open("song.mp3");
//   if (!r) { std::cerr << r.Message() << "\n"; return; }
//   const auto& info = reader.GetInfo();
//   std::vector<float> samples;
//   reader.ReadTimeRangeMs(1, 23, samples); // [1ms, 23ms)
//   reader.SetTargetSampleRate(22050);     // 44.1k -> 22.05k 下采样
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "audio_resover/AudioError.hpp"
#include "audio_resover/AudioFormat.hpp"
#include "audio_resover/AudioInfo.hpp"
#include "audio_resover/IAudioDecoder.hpp"
#include "audio_resover/IAudioResampler.hpp"

namespace audio_resover
{

class AudioReader
{
  public:
	AudioReader();
	~AudioReader();

	AudioReader(const AudioReader&) = delete;
	AudioReader& operator=(const AudioReader&) = delete;
	AudioReader(AudioReader&&) noexcept;
	AudioReader& operator=(AudioReader&&) noexcept;

	// ====== 0. 打开 / 关闭 ======
	// 按路径打开文件。默认使用内部 MiniaudioDecoder。
	Result<void> Open(const std::string& filePath);

	// 在 Open() 之前替换内部解码器为自定义实现。
	// 传入 nullptr 会恢复为默认 MiniaudioDecoder。
	// 若 Open() 已经被调用，返回 AlreadyOpened。
	Result<void> SetDecoder(std::unique_ptr<IAudioDecoder> decoder);

	bool IsOpen() const;
	void Close();

	// ====== 1. 元数据 ======
	const AudioInfo& GetInfo() const;

	// ====== 2. 顺序 / 随机读取 ======
	// 读取 frameCount 个帧（F32）写入 outSamples。outSamples 会被 resize 到
	// 恰好 (framesRead * channels) 个元素。
	Result<std::uint64_t> ReadFrames(std::uint64_t frameCount, std::vector<float>& outSamples);

	// 读取 frameCount 个帧，按调用方指定的采样格式写入 outBytes（原始字节
	// 缓冲区）。outBytes 会被 resize 到实际字节数。
	Result<std::uint64_t> ReadFrames(std::uint64_t frameCount, SampleFormat fmt,
									 std::vector<std::uint8_t>& outBytes);

	// 随机读取 [startMs, endMs) 区间到 outSamples（F32）。内部先 seek 再读，
	// 返回实际解码的帧数。
	Result<std::uint64_t> ReadTimeRangeMs(std::uint64_t startMs, std::uint64_t endMs,
										  std::vector<float>& outSamples);

	// 位置 / 跳转。
	Result<void> SeekToMs(std::uint64_t ms);
	Result<void> SeekToFrame(std::uint64_t frameIndex);
	std::uint64_t GetPositionMs() const;
	std::uint64_t GetPositionFrames() const;
	bool HasMore() const;

	// ====== 3. 重采样（仅下采样）======
	// 注入自定义重采样器。传入 nullptr 会清除重采样并把目标采样率重置为 0
	// （即按原始采样率读取）。
	Result<void> SetResampler(std::unique_ptr<IAudioResampler> resampler);

	// 设置目标采样率。若未注入重采样器，内部会创建默认的
	// MiniaudioResampler（线性）。若 targetRate > 源采样率，返回
	// UnsupportedResampleDirection。
	Result<void> SetTargetSampleRate(std::uint32_t targetSampleRate);

	std::uint32_t GetTargetSampleRate() const;

  private:
	// 用当前源 / 目标参数重新配置已激活的重采样器。
	Result<void> ReconfigureResampler();

	std::unique_ptr<IAudioDecoder> mDecoder;
	std::unique_ptr<IAudioResampler> mResampler;
	std::uint32_t mTargetSampleRate = 0;
	bool mUsingDefaultResampler = false;
};

} // namespace audio_resover
