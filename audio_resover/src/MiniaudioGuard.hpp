// audio_resover - C++17 audio processing library
// Private header: miniaudio RAII guard (extension hook).
//
// miniaudio's decoder and data_converter APIs do NOT require a global
// context (ma_context) for file-based decoding, so this guard is currently
// a no-op. It is retained as an extension point for future backends that
// may require global init/teardown (e.g. device playback).
#pragma once

namespace audio_resover
{

class MiniaudioGuard
{
  public:
	// Acquire any future global miniaudio resources. Currently a no-op.
	MiniaudioGuard() = default;
	// Release any future global miniaudio resources. Currently a no-op.
	~MiniaudioGuard() = default;

	MiniaudioGuard(const MiniaudioGuard&) = delete;
	MiniaudioGuard& operator=(const MiniaudioGuard&) = delete;
};

} // namespace audio_resover
