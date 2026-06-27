// audio_resover - C++17 audio processing library
// Public header: AudioReader high-level facade.
//
// AudioReader is the main user-facing entry point. It composes an
// IAudioDecoder (default: MiniaudioDecoder) with an optional IAudioResampler
// (default: MiniaudioResampler) and exposes convenience APIs in both frame
// and millisecond domains.
//
// Typical usage:
//   AudioReader reader;
//   auto r = reader.Open("song.mp3");
//   if (!r) { std::cerr << r.Message() << "\n"; return; }
//   const auto& info = reader.GetInfo();
//   std::vector<float> samples;
//   reader.ReadTimeRangeMs(1, 23, samples); // [1ms, 23ms)
//   reader.SetTargetSampleRate(22050);     // downsample 44.1k -> 22.05k
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

	// ====== 0. Open / close ======
	// Open a file by path. Uses the internal MiniaudioDecoder by default.
	Result<void> Open(const std::string& filePath);

	// Replace the internal decoder before Open() with a custom one.
	// Passing nullptr restores the default MiniaudioDecoder. Returns
	// AlreadyOpened if Open() has already been called.
	Result<void> SetDecoder(std::unique_ptr<IAudioDecoder> decoder);

	bool IsOpen() const;
	void Close();

	// ====== 1. Metadata ======
	const AudioInfo& GetInfo() const;

	// ====== 2. Sequential / random reads ======
	// Read frameCount frames (F32) into outSamples. outSamples is resized to
	// exactly (framesRead * channels) elements.
	Result<std::uint64_t> ReadFrames(std::uint64_t frameCount, std::vector<float>& outSamples);

	// Read frameCount frames in a caller-specified sample format into
	// outBytes (raw byte buffer). outBytes is resized to the actual byte size.
	Result<std::uint64_t> ReadFrames(std::uint64_t frameCount, SampleFormat fmt,
									 std::vector<std::uint8_t>& outBytes);

	// Random read of [startMs, endMs) into outSamples (F32). Seeks the decoder,
	// reads, and returns the number of frames actually decoded.
	Result<std::uint64_t> ReadTimeRangeMs(std::uint64_t startMs, std::uint64_t endMs,
										  std::vector<float>& outSamples);

	// Position / seek.
	Result<void> SeekToMs(std::uint64_t ms);
	Result<void> SeekToFrame(std::uint64_t frameIndex);
	std::uint64_t GetPositionMs() const;
	std::uint64_t GetPositionFrames() const;
	bool HasMore() const;

	// ====== 3. Resampling (downsampling only) ======
	// Inject a custom resampler. nullptr clears resampling and resets the
	// target sample rate to 0 (i.e. read at native rate).
	Result<void> SetResampler(std::unique_ptr<IAudioResampler> resampler);

	// Set a target sample rate. If no resampler has been injected, a default
	// MiniaudioResampler (linear) is created internally. Returns
	// UnsupportedResampleDirection if targetRate > source sample rate.
	Result<void> SetTargetSampleRate(std::uint32_t targetSampleRate);

	std::uint32_t GetTargetSampleRate() const;

  private:
	// Reconfigure the active resampler with current source/target params.
	Result<void> ReconfigureResampler();

	std::unique_ptr<IAudioDecoder> mDecoder;
	std::unique_ptr<IAudioResampler> mResampler;
	std::uint32_t mTargetSampleRate = 0;
	bool mUsingDefaultResampler = false;
};

} // namespace audio_resover
