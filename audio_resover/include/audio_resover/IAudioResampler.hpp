// audio_resover - C++17 audio processing library
// Public header: resampler strategy interface.
//
// IAudioResampler abstracts sample-rate conversion. The default implementation
// (MiniaudioResampler) uses miniaudio's linear resampler. Users may plug in
// their own (e.g. higher-quality polyphase, FFT-based) implementation by
// implementing this interface.
//
// Contract:
//   - Only downsampling is required (outSampleRate <= inSampleRate).
//   - Sample format and channel count are preserved (no format/channel mixing).
//   - Process() must be callable incrementally; internal state carries over.
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

	// Configure for a new (inRate -> outRate, channels, format) tuple.
	// Resets internal state. Returns UnsupportedResampleDirection if
	// outSampleRate > inSampleRate.
	virtual Result<void> Configure(std::uint32_t inSampleRate, std::uint32_t outSampleRate,
								   std::uint32_t channels, SampleFormat format) = 0;

	// Process inFrameCount frames from inBuffer into outBuffer (capacity
	// outFrameCountCap). Returns the number of frames written to outBuffer.
	// Note: a single Process call may consume all input but produce less than
	// the cap; trailing samples may require a final flush call (impl-defined).
	virtual Result<std::uint64_t> Process(const void* inBuffer, std::uint64_t inFrameCount,
										  void* outBuffer, std::uint64_t outFrameCountCap) = 0;

	// Best-effort estimate of output frames for a given input frame count,
	// for buffer sizing. Implementation may round down.
	virtual std::uint64_t EstimateOutFrames(std::uint64_t inFrameCount) const = 0;
};

} // namespace audio_resover
