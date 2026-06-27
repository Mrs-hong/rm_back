// audio_resover - C++17 音频处理库
// 公开头文件：重采样策略接口。
//
// IAudioResampler 抽象了采样率转换。默认实现 MiniaudioResampler 使用
// miniaudio 的线性重采样器。用户可自行实现本接口接入更高品质的
// （例如多相滤波、FFT-based）实现。
//
// 契约：
//   - 仅要求下采样（outSampleRate <= inSampleRate）。
//   - 采样格式与通道数保持不变（不做格式 / 通道转换）。
//   - Process() 必须可增量调用；内部状态跨调用保留。
#pragma once

#include <cstdint>

#include "audio_resover/AudioError.hpp"
#include "audio_resover/AudioFormat.hpp"

namespace audio_resover
{

class IAudioResampler
{
  public:
	virtual ~IAudioResampler() = default;

	// 为新的 (inRate -> outRate, channels, format) 组合进行配置。
	// 会重置内部状态。若 outSampleRate > inSampleRate 则返回
	// UnsupportedResampleDirection。
	virtual Result<void> Configure(std::uint32_t inSampleRate, std::uint32_t outSampleRate,
								   std::uint32_t channels, SampleFormat format) = 0;

	// 从 inBuffer 处理 inFrameCount 个帧，写入 outBuffer（容量 outFrameCountCap）。
	// 返回实际写入 outBuffer 的帧数。
	// 注意：单次 Process 可能消费完全部输入但产出少于 cap 的帧；
	// 剩余样本可能需要一次 flush 调用（具体由实现决定）。
	virtual Result<std::uint64_t> Process(const void* inBuffer, std::uint64_t inFrameCount,
										  void* outBuffer, std::uint64_t outFrameCountCap) = 0;

	// 用于缓冲区分配的输出帧数预估（best-effort）。实现可向下取整。
	virtual std::uint64_t EstimateOutFrames(std::uint64_t inFrameCount) const = 0;
};

} // namespace audio_resover
