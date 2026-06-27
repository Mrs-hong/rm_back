// audio_resover - C++17 audio processing library
// Private header: miniaudio-backed IAudioDecoder implementation.
#pragma once

#include <cstdint>
#include <string>

#include "audio_resover/AudioError.hpp"
#include "audio_resover/AudioFormat.hpp"
#include "audio_resover/AudioInfo.hpp"
#include "audio_resover/IAudioDecoder.hpp"

// Forward-declare the miniaudio type so the public PIMPL does not leak
// miniaudio symbols into the rest of the project.
struct ma_decoder;

namespace audio_resover
{

class MiniaudioDecoder final : public IAudioDecoder
{
  public:
	MiniaudioDecoder();
	~MiniaudioDecoder() override;

	MiniaudioDecoder(const MiniaudioDecoder&) = delete;
	MiniaudioDecoder& operator=(const MiniaudioDecoder&) = delete;

	Result<void> Open(const std::string& filePath) override;
	bool IsOpen() const override;
	void Close() override;
	const AudioInfo& GetInfo() const override;
	Result<std::uint64_t> ReadFrames(std::uint64_t frameCount, SampleFormat outFormat,
									 void* outBuffer) override;
	Result<void> SeekToFrame(std::uint64_t frameIndex) override;
	std::uint64_t GetPositionFrames() const override;

  private:
	// Sniff the first bytes of the file to determine its real container format
	// (independent of file extension).
	static ContainerFormat SniffContainerFormat(const std::string& filePath,
												std::uint64_t fileSize);

	// Allocate and configure the underlying ma_decoder. Returns nullptr on
	// failure (caller translates the error code).
	bool InitMiniaudioDecoder(const std::string& filePath);

	ma_decoder* mDecoder = nullptr; // owned; freed in Close()/dtor
	AudioInfo mInfo{};
	bool mIsOpen = false;
};

} // namespace audio_resover
