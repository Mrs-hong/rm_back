// audio_resover - C++17 音频处理库
// 私有头文件：通道混合器（F32 交错布局下混到单声道）。
//
// 设计目标：
//   - 仅支持 F32 交错输入 / 输出
//   - 支持 in-place 转换（inBuf == outBuf），输出长度 <= 输入长度
//   - 三种模式：Average（等权平均）、First（取第 0 通道）、Last（取末通道）
//
// in-place 安全性证明：
//   写入 out[i] 时，后续只读 in[(i+1)*channels + c]。
//   由于 channels >= 2（srcChannels==1 已被上层直通），
//   (i+1)*channels >= (i+1)*2 > i+1 > i，覆盖不会影响后续读取。
#pragma once

#include <cstdint>

#include "audio_resover/AudioChannelConverter.hpp"

namespace audio_resover
{

class ChannelMixer
{
  public:
	// 把 inFrames 个交错 F32 帧从 srcChannels 通道下混到 1 通道。
	// outBuf 至少能容纳 inFrames 个 float。
	// 支持 in-place（inBuf == outBuf）。
	// 返回 true 表示成功；false 表示参数非法。
	static bool MixToMono(const float* inBuf, std::uint64_t inFrames, std::uint32_t srcChannels,
						  ChannelMixMode mode, float* outBuf);

  private:
	static void MixAverage(const float* in, std::uint64_t frames, std::uint32_t channels,
						   float* out);
	static void MixFirst(const float* in, std::uint64_t frames, std::uint32_t channels, float* out);
	static void MixLast(const float* in, std::uint64_t frames, std::uint32_t channels, float* out);
};

} // namespace audio_resover
