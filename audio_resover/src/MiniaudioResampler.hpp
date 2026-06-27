// audio_resover - C++17 audio processing library
// Private header: miniaudio-backed IAudioResampler implementation.
//
// Note: ma_data_converter in miniaudio is an *anonymous* typedef'd struct
// (no struct tag), so it cannot be forward-declared in C++ without conflicting
// with the typedef. We therefore store the converter as an opaque void* and
// cast to ma_data_converter* inside the .cpp where miniaudio.h is included.
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
	// Cast to ma_data_converter* in the .cpp (miniaudio.h is included there).
	void* mConverterRaw = nullptr; // owned; opaque pointer to ma_data_converter
	std::uint32_t mInSampleRate = 0;
	std::uint32_t mOutSampleRate = 0;
	std::uint32_t mChannels = 0;
	SampleFormat mFormat = SampleFormat::Unknown;
};

} // namespace audio_resover
