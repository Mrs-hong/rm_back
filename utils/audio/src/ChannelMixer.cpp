// utils::audio - C++17 音频处理库
// 实现文件：通道混合器。
#include "ChannelMixer.hpp"

#include <cstring>

namespace utils::audio
{

bool ChannelMixer::MixToMono(const float* inBuf, std::uint64_t inFrames, std::uint32_t srcChannels,
							 ChannelMixMode mode, float* outBuf)
{
	if (inBuf == nullptr || outBuf == nullptr || srcChannels == 0) {
		return false;
	}

	// 单通道直通：直接拷贝（in != out 时）或零操作（in == out 时）。
	if (srcChannels == 1) {
		if (inBuf != outBuf) {
			std::memcpy(outBuf, inBuf, static_cast<std::size_t>(inFrames) * sizeof(float));
		}
		return true;
	}

	switch (mode) {
	case ChannelMixMode::Average:
		MixAverage(inBuf, inFrames, srcChannels, outBuf);
		return true;
	case ChannelMixMode::First:
		MixFirst(inBuf, inFrames, srcChannels, outBuf);
		return true;
	case ChannelMixMode::Last:
		MixLast(inBuf, inFrames, srcChannels, outBuf);
		return true;
	}
	return false;
}

void ChannelMixer::MixAverage(const float* in, std::uint64_t frames, std::uint32_t channels,
							  float* out)
{
	// 等权平均：(s0 + s1 + ... + s_{N-1}) / N
	// 用乘以 1/N 替代除法，便于编译器向量化。
	const float inv = 1.0f / static_cast<float>(channels);
	for (std::uint64_t i = 0; i < frames; ++i) {
		const float* frame = in + i * channels;
		float sum = 0.0f;
		for (std::uint32_t c = 0; c < channels; ++c) {
			sum += frame[c];
		}
		out[i] = sum * inv;
	}
}

void ChannelMixer::MixFirst(const float* in, std::uint64_t frames, std::uint32_t channels,
							float* out)
{
	// stride 拷贝：每 channels 个采样取第 0 个。
	// 编译器可识别为 stride-1 模式并自动向量化。
	for (std::uint64_t i = 0; i < frames; ++i) {
		out[i] = in[i * channels];
	}
}

void ChannelMixer::MixLast(const float* in, std::uint64_t frames, std::uint32_t channels,
						   float* out)
{
	// stride 拷贝：每 channels 个采样取第 (channels-1) 个。
	const std::uint32_t lastIdx = channels - 1;
	for (std::uint64_t i = 0; i < frames; ++i) {
		out[i] = in[i * channels + lastIdx];
	}
}

} // namespace utils::audio
