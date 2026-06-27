// audio_resover - C++17 audio processing library
// Public header: sample / container format enums.
#pragma once

namespace audio_resover
{

// PCM sample format. Determines how each individual sample is laid out in
// memory. F32 is the recommended output format ([-1.0, 1.0] normalised).
enum class SampleFormat : int {
	Unknown = 0,
	U8,	 // 8-bit unsigned PCM, [0, 255]
	S16, // 16-bit signed PCM
	S24, // 24-bit signed PCM (packed, 3 bytes per sample)
	S32, // 32-bit signed PCM
	F32, // 32-bit IEEE float, [-1.0, 1.0] (default for ReadFrames)
};

// Audio container / coding format. This is the *real* format of the file as
// detected from its content (independent of the file extension).
enum class ContainerFormat : int {
	Unknown = 0,
	Wav,	// RIFF/WAVE (PCM, ADPCM, IEEE float, etc.)
	Mp3,	// MPEG audio layer III
	Flac,	// Free Lossless Audio Codec
	Vorbis, // Ogg Vorbis
	Raw,	// Headerless raw PCM
};

inline const char* ToString(SampleFormat f)
{
	switch (f) {
	case SampleFormat::Unknown:
		return "Unknown";
	case SampleFormat::U8:
		return "U8";
	case SampleFormat::S16:
		return "S16";
	case SampleFormat::S24:
		return "S24";
	case SampleFormat::S32:
		return "S32";
	case SampleFormat::F32:
		return "F32";
	}
	return "Unknown";
}

inline const char* ToString(ContainerFormat f)
{
	switch (f) {
	case ContainerFormat::Unknown:
		return "Unknown";
	case ContainerFormat::Wav:
		return "WAV";
	case ContainerFormat::Mp3:
		return "MP3";
	case ContainerFormat::Flac:
		return "FLAC";
	case ContainerFormat::Vorbis:
		return "Vorbis";
	case ContainerFormat::Raw:
		return "Raw";
	}
	return "Unknown";
}

} // namespace audio_resover
