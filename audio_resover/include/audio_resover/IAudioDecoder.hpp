// audio_resover - C++17 audio processing library
// Public header: decoder strategy interface.
//
// IAudioDecoder abstracts the codec-specific act of opening an audio file,
// querying its metadata, and reading PCM frames. The default implementation
// (MiniaudioDecoder) covers WAV/MP3/FLAC/Vorbis via miniaudio; users may plug
// in their own backend (e.g. for AAC, Opus, a hardware decoder) by implementing
// this interface.
#pragma once

#include <cstdint>
#include <string>

#include "audio_resover/AudioError.hpp"
#include "audio_resover/AudioFormat.hpp"
#include "audio_resover/AudioInfo.hpp"

namespace audio_resover
{

class IAudioDecoder
{
  public:
	virtual ~IAudioDecoder() = default;

	// Open a file by path. On success GetInfo() returns valid metadata.
	// If the file cannot be parsed at all, returns UnknownFormat / CorruptedFile.
	virtual Result<void> Open(const std::string& filePath) = 0;

	virtual bool IsOpen() const = 0;
	virtual void Close() = 0;

	// Metadata. Valid only after a successful Open().
	virtual const AudioInfo& GetInfo() const = 0;

	// Read up to frameCount PCM frames sequentially into outBuffer.
	// outFormat determines the sample layout in outBuffer; miniaudio-backed
	// implementations perform automatic sample-format conversion.
	// Returns the number of frames actually read (may be less than requested
	// at EOF). At EOF, returns Ok with value 0.
	virtual Result<std::uint64_t> ReadFrames(std::uint64_t frameCount, SampleFormat outFormat,
											 void* outBuffer) = 0;

	// Seek the read pointer to an absolute PCM frame index.
	virtual Result<void> SeekToFrame(std::uint64_t frameIndex) = 0;

	// Current read pointer position in PCM frames.
	virtual std::uint64_t GetPositionFrames() const = 0;
};

} // namespace audio_resover
