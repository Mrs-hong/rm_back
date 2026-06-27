// audio_resover - C++17 audio processing library
// Public header: audio metadata POD.
#pragma once

#include <cstdint>

#include "audio_resover/AudioFormat.hpp"

namespace audio_resover
{

// Aggregated metadata for an opened audio file. All fields are populated by
// IAudioDecoder::Open; consumers should treat the struct as read-only.
//
// Notes:
//   - IsValid:      true if the file was parsed successfully and at least the
//                   format / sample rate / channels could be determined.
//   - IsCorrupted:  true if any structural / sync / CRC error was detected
//                   during decode. Reads may still succeed partially.
//   - DurationMs:    derived from TotalFrames and SampleRate; 0 if unknown.
//   - AvgBitRateKbps: average bitrate for compressed formats; 0 for raw PCM.
struct AudioInfo {
	bool IsValid = false;
	bool IsCorrupted = false;

	ContainerFormat Format = ContainerFormat::Unknown;
	SampleFormat NativeSampleFormat = SampleFormat::Unknown;

	std::uint32_t SampleRate = 0;	 // Hz
	std::uint32_t Channels = 0;		 // 1 = mono, 2 = stereo, ...
	std::uint32_t BitsPerSample = 0; // 0 if not applicable (compressed)

	std::uint64_t TotalFrames = 0; // one frame = one sample per channel
	std::uint64_t DurationMs = 0;
	std::uint64_t FileSizeBytes = 0;

	std::uint32_t AvgBitRateKbps = 0;
};

} // namespace audio_resover
