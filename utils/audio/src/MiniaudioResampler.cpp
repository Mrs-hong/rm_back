// utils::audio - C++17 音频处理库
// 实现文件：基于 miniaudio 的 IAudioResampler。
#include "MiniaudioResampler.hpp"

#include <new>
#include <string>

#include "miniaudio.h"

namespace utils::audio
{

namespace
{

ma_format ToMaFormat(SampleFormat f)
{
	switch (f) {
	case SampleFormat::U8:
		return ma_format_u8;
	case SampleFormat::S16:
		return ma_format_s16;
	case SampleFormat::S24:
		return ma_format_s24;
	case SampleFormat::S32:
		return ma_format_s32;
	case SampleFormat::F32:
		return ma_format_f32;
	case SampleFormat::Unknown:
		return ma_format_unknown;
	}
	return ma_format_unknown;
}

// 把不透明 void* 成员便捷地 cast 回 ma_data_converter*。
inline ma_data_converter* AsConverter(void* p)
{
	return static_cast<ma_data_converter*>(p);
}

} // namespace

MiniaudioResampler::MiniaudioResampler() = default;

MiniaudioResampler::~MiniaudioResampler()
{
	if (mConverterRaw) {
		ma_data_converter_uninit(AsConverter(mConverterRaw), nullptr);
		// ma_data_converter 用 operator new 分配；这里 cast 回来释放。
		delete AsConverter(mConverterRaw);
		mConverterRaw = nullptr;
	}
}

Result<void> MiniaudioResampler::Configure(std::uint32_t inSampleRate, std::uint32_t outSampleRate,
										   std::uint32_t channels, SampleFormat format)
{
	if (inSampleRate == 0 || outSampleRate == 0 || channels == 0) {
		return Result<void>::Fail(ErrorCode::InvalidArgument,
								  "sample rate / channels must be > 0");
	}
	if (format == SampleFormat::Unknown) {
		return Result<void>::Fail(ErrorCode::InvalidArgument, "format must not be Unknown");
	}
	// 项目范围限制：仅支持下采样。
	if (outSampleRate > inSampleRate) {
		return Result<void>::Fail(ErrorCode::UnsupportedResampleDirection,
								  "outSampleRate (" + std::to_string(outSampleRate) +
									  ") > inSampleRate (" + std::to_string(inSampleRate) +
									  "); upsampling is not supported");
	}

	// 销毁已有的 converter。
	if (mConverterRaw) {
		ma_data_converter_uninit(AsConverter(mConverterRaw), nullptr);
		delete AsConverter(mConverterRaw);
		mConverterRaw = nullptr;
	}

	const ma_format maFmt = ToMaFormat(format);
	ma_data_converter_config config = ma_data_converter_config_init(
		maFmt, maFmt, channels, channels, inSampleRate, outSampleRate);
	// 使用线性重采样器（miniaudio 默认）；显式指定以防外部配置静默改变行为。
	config.resampling.algorithm = ma_resample_algorithm_linear;

	auto* conv = new (std::nothrow) ma_data_converter;
	if (conv == nullptr) {
		return Result<void>::Fail(ErrorCode::ResamplerInitFailed, "converter alloc failed");
	}

	const ma_result mr = ma_data_converter_init(&config, nullptr, conv);
	if (mr != MA_SUCCESS) {
		delete conv;
		return Result<void>::Fail(ErrorCode::ResamplerInitFailed,
								  "ma_data_converter_init failed (code=" + std::to_string(mr) +
									  ")");
	}

	mConverterRaw = conv;
	mInSampleRate = inSampleRate;
	mOutSampleRate = outSampleRate;
	mChannels = channels;
	mFormat = format;
	return Result<void>::Ok();
}

Result<std::uint64_t> MiniaudioResampler::Process(const void* inBuffer, std::uint64_t inFrameCount,
												  void* outBuffer, std::uint64_t outFrameCountCap)
{
	if (mConverterRaw == nullptr) {
		return Result<std::uint64_t>::Fail(ErrorCode::ResamplerNotSet,
										   "Configure() has not been called");
	}
	if (inFrameCount == 0 || inBuffer == nullptr) {
		return Result<std::uint64_t>::Ok(0);
	}
	if (outBuffer == nullptr || outFrameCountCap == 0) {
		return Result<std::uint64_t>::Fail(ErrorCode::InvalidArgument,
										   "outBuffer / outFrameCountCap invalid");
	}

	ma_uint64 inFrames = static_cast<ma_uint64>(inFrameCount);
	ma_uint64 outFrames = static_cast<ma_uint64>(outFrameCountCap);
	const ma_result mr = ma_data_converter_process_pcm_frames(AsConverter(mConverterRaw), inBuffer,
															  &inFrames, outBuffer, &outFrames);
	if (mr != MA_SUCCESS) {
		return Result<std::uint64_t>::Fail(
			ErrorCode::ResampleFailed,
			"ma_data_converter_process_pcm_frames failed (code=" + std::to_string(mr) + ")");
	}
	return Result<std::uint64_t>::Ok(outFrames);
}

std::uint64_t MiniaudioResampler::EstimateOutFrames(std::uint64_t inFrameCount) const
{
	if (mInSampleRate == 0 || mOutSampleRate == 0)
		return 0;
	return inFrameCount * mOutSampleRate / mInSampleRate;
}

} // namespace utils::audio
