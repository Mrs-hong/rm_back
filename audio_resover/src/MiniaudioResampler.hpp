// audio_resover - C++17 音频处理库
// 私有头文件：基于 miniaudio 的 IAudioResampler 实现。
//
// 注意：miniaudio 中的 ma_data_converter 是一个*匿名* typedef 结构体
// （没有 struct tag），所以在 C++ 中无法在不与 typedef 冲突的前提下
// 前向声明。因此这里把 converter 存为不透明 void*，在 .cpp 中
// （那里 include 了 miniaudio.h）再 cast 回 ma_data_converter*。
#pragma once

#include <cstdint>

#include "audio_resover/AudioError.hpp"
#include "audio_resover/AudioFormat.hpp"
#include "audio_resover/IAudioResampler.hpp"

namespace audio_resover
{

class MiniaudioResampler final : public IAudioResampler
{
  public:
	MiniaudioResampler();
	~MiniaudioResampler() override;

	MiniaudioResampler(const MiniaudioResampler&) = delete;
	MiniaudioResampler& operator=(const MiniaudioResampler&) = delete;

	Result<void> Configure(std::uint32_t inSampleRate, std::uint32_t outSampleRate,
						   std::uint32_t channels, SampleFormat format) override;
	Result<std::uint64_t> Process(const void* inBuffer, std::uint64_t inFrameCount, void* outBuffer,
								  std::uint64_t outFrameCountCap) override;
	std::uint64_t EstimateOutFrames(std::uint64_t inFrameCount) const override;

  private:
	// 在 .cpp 中转换为 ma_data_converter*（miniaudio.h 在那里 include）。
	void* mConverterRaw = nullptr; // 持有所有权；指向 ma_data_converter 的不透明指针
	std::uint32_t mInSampleRate = 0;
	std::uint32_t mOutSampleRate = 0;
	std::uint32_t mChannels = 0;
	SampleFormat mFormat = SampleFormat::Unknown;
};

} // namespace audio_resover
