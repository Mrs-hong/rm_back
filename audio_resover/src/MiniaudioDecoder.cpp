// audio_resover - C++17 audio processing library
// Implementation: miniaudio-backed IAudioDecoder.
#include "MiniaudioDecoder.hpp"

#include <sys/stat.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

// Include miniaudio as header-only (no MINIAUDIO_IMPLEMENTATION here - that
// lives in miniaudio_impl.cpp). We only need the API declarations.
#include "miniaudio.h"

namespace audio_resover
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

SampleFormat FromMaFormat(ma_format f)
{
	switch (f) {
	case ma_format_u8:
		return SampleFormat::U8;
	case ma_format_s16:
		return SampleFormat::S16;
	case ma_format_s24:
		return SampleFormat::S24;
	case ma_format_s32:
		return SampleFormat::S32;
	case ma_format_f32:
		return SampleFormat::F32;
	default:
		return SampleFormat::Unknown;
	}
}

std::uint32_t BitsForFormat(SampleFormat f)
{
	switch (f) {
	case SampleFormat::U8:
		return 8;
	case SampleFormat::S16:
		return 16;
	case SampleFormat::S24:
		return 24;
	case SampleFormat::S32:
		return 32;
	case SampleFormat::F32:
		return 32;
	case SampleFormat::Unknown:
		return 0;
	}
	return 0;
}

// Get file size in bytes; 0 on failure (treated as unknown).
std::uint64_t GetFileSizeBytes(const std::string& filePath)
{
	struct stat st;
	if (stat(filePath.c_str(), &st) != 0)
		return 0;
	return static_cast<std::uint64_t>(st.st_size);
}

} // namespace

MiniaudioDecoder::MiniaudioDecoder() = default;

MiniaudioDecoder::~MiniaudioDecoder()
{
	Close();
}

ContainerFormat MiniaudioDecoder::SniffContainerFormat(const std::string& filePath,
													   std::uint64_t /*fileSize*/)
{
	std::ifstream f(filePath, std::ios::binary);
	if (!f)
		return ContainerFormat::Unknown;

	std::uint8_t hdr[16]{};
	f.read(reinterpret_cast<char*>(hdr), sizeof(hdr));
	const auto got = static_cast<std::size_t>(f.gcount());
	if (got < 4)
		return ContainerFormat::Unknown;

	// RIFF....WAVE
	if (got >= 12 && std::memcmp(hdr, "RIFF", 4) == 0 && std::memcmp(hdr + 8, "WAVE", 4) == 0) {
		return ContainerFormat::Wav;
	}
	// OggS container (we assume Vorbis; miniaudio native Ogg support is Vorbis)
	if (std::memcmp(hdr, "OggS", 4) == 0) {
		return ContainerFormat::Vorbis;
	}
	// fLaC
	if (std::memcmp(hdr, "fLaC", 4) == 0) {
		return ContainerFormat::Flac;
	}
	// ID3v2 tag at start of MP3
	if (std::memcmp(hdr, "ID3", 3) == 0) {
		return ContainerFormat::Mp3;
	}
	// Raw MP3 frame sync: 0xFF followed by 0xE0..0xFF (11-bit sync word).
	if (hdr[0] == 0xFF && (hdr[1] & 0xE0) == 0xE0) {
		return ContainerFormat::Mp3;
	}
	return ContainerFormat::Unknown;
}

Result<void> MiniaudioDecoder::Open(const std::string& filePath)
{
	if (mIsOpen) {
		return Result<void>::Fail(AudioErrorCode::AlreadyOpened);
	}

	// Existence check (more specific than miniaudio's MA_DOES_NOT_EXIST).
	std::ifstream test(filePath, std::ios::binary);
	if (!test) {
		return Result<void>::Fail(AudioErrorCode::FileNotFound, "File not found: " + filePath);
	}
	test.close();

	mInfo = AudioInfo{};
	mInfo.FileSizeBytes = GetFileSizeBytes(filePath);
	mInfo.Format = SniffContainerFormat(filePath, mInfo.FileSizeBytes);

	// Init miniaudio decoder with outputFormat=unknown to preserve native
	// sample format; miniaudio will keep the file's native format/channels/rate.
	ma_decoder_config config = ma_decoder_config_init(ma_format_unknown, 0, 0);
	mDecoder = new (std::nothrow) ma_decoder;
	if (mDecoder == nullptr) {
		return Result<void>::Fail(AudioErrorCode::InternalError, "decoder alloc failed");
	}

	ma_result mr = ma_decoder_init_file(filePath.c_str(), &config, mDecoder);
	if (mr != MA_SUCCESS) {
		delete mDecoder;
		mDecoder = nullptr;
		if (mr == MA_INVALID_FILE || mr == MA_NOT_IMPLEMENTED) {
			return Result<void>::Fail(AudioErrorCode::UnknownFormat,
									  "miniaudio did not recognise file format");
		}
		return Result<void>::Fail(AudioErrorCode::FileOpenFailed,
								  "ma_decoder_init_file failed (code=" + std::to_string(mr) + ")");
	}

	// Probe native data format.
	ma_format nativeFmt = ma_format_unknown;
	ma_uint32 channels = 0;
	ma_uint32 sampleRate = 0;
	mr = ma_decoder_get_data_format(mDecoder, &nativeFmt, &channels, &sampleRate, nullptr, 0);
	if (mr != MA_SUCCESS || nativeFmt == ma_format_unknown || channels == 0 || sampleRate == 0) {
		ma_decoder_uninit(mDecoder);
		delete mDecoder;
		mDecoder = nullptr;
		return Result<void>::Fail(AudioErrorCode::CorruptedFile,
								  "could not determine audio data format");
	}

	mInfo.NativeSampleFormat = FromMaFormat(nativeFmt);
	mInfo.SampleRate = sampleRate;
	mInfo.Channels = channels;
	mInfo.BitsPerSample = BitsForFormat(mInfo.NativeSampleFormat);

	// Total frames / duration.
	ma_uint64 totalFrames = 0;
	mr = ma_decoder_get_length_in_pcm_frames(mDecoder, &totalFrames);
	if (mr == MA_SUCCESS && totalFrames > 0) {
		mInfo.TotalFrames = totalFrames;
		mInfo.DurationMs = totalFrames * 1000ULL / sampleRate;
	}

	// Average bitrate for compressed formats (PCM = 0).
	if (mInfo.Format != ContainerFormat::Wav && mInfo.Format != ContainerFormat::Raw &&
		mInfo.DurationMs > 0 && mInfo.FileSizeBytes > 0) {
		// bits/sec = (size_bytes * 8) / (duration_sec) ; kbps = bits/sec / 1000
		mInfo.AvgBitRateKbps = static_cast<std::uint32_t>((mInfo.FileSizeBytes * 8) /
														  (mInfo.DurationMs)); // bytes*8/ms = kbps
	}

	mInfo.IsValid = true;
	mInfo.IsCorrupted = false;
	mIsOpen = true;
	return Result<void>::Ok();
}

bool MiniaudioDecoder::IsOpen() const
{
	return mIsOpen;
}

void MiniaudioDecoder::Close()
{
	if (mDecoder) {
		ma_decoder_uninit(mDecoder);
		delete mDecoder;
		mDecoder = nullptr;
	}
	mInfo = AudioInfo{};
	mIsOpen = false;
}

const AudioInfo& MiniaudioDecoder::GetInfo() const
{
	return mInfo;
}

Result<std::uint64_t> MiniaudioDecoder::ReadFrames(std::uint64_t frameCount, SampleFormat outFormat,
												   void* outBuffer)
{
	if (!mIsOpen || mDecoder == nullptr) {
		return Result<std::uint64_t>::Fail(AudioErrorCode::NotOpened);
	}
	if (outFormat == SampleFormat::Unknown) {
		return Result<std::uint64_t>::Fail(AudioErrorCode::InvalidArgument,
										   "outFormat must not be Unknown");
	}
	if (frameCount == 0 || outBuffer == nullptr) {
		return Result<std::uint64_t>::Ok(0);
	}

	const ma_format nativeFmt = ToMaFormat(mInfo.NativeSampleFormat);
	const ma_format reqFmt = ToMaFormat(outFormat);

	if (reqFmt == nativeFmt) {
		// Direct read - no format conversion needed.
		ma_uint64 framesRead = 0;
		const ma_result mr =
			ma_decoder_read_pcm_frames(mDecoder, outBuffer, frameCount, &framesRead);
		if (mr != MA_SUCCESS && mr != MA_AT_END) {
			mInfo.IsCorrupted = true;
			return Result<std::uint64_t>::Fail(
				AudioErrorCode::DecodeFailed,
				"ma_decoder_read_pcm_frames failed (code=" + std::to_string(mr) + ")");
		}
		if (framesRead < frameCount && mr != MA_AT_END) {
			// Premature EOF / decode error: mark as possibly corrupted.
			mInfo.IsCorrupted = true;
		}
		return Result<std::uint64_t>::Ok(framesRead);
	}

	// Need format conversion: read in native format, then convert.
	const std::size_t nativeSampleSize = ma_get_bytes_per_sample(nativeFmt);
	if (nativeSampleSize == 0) {
		return Result<std::uint64_t>::Fail(AudioErrorCode::InternalError,
										   "unknown native sample size");
	}
	const std::size_t totalSamples = frameCount * mInfo.Channels;
	std::vector<std::uint8_t> nativeBuf(totalSamples * nativeSampleSize);

	ma_uint64 framesRead = 0;
	const ma_result mr =
		ma_decoder_read_pcm_frames(mDecoder, nativeBuf.data(), frameCount, &framesRead);
	if (mr != MA_SUCCESS && mr != MA_AT_END) {
		mInfo.IsCorrupted = true;
		return Result<std::uint64_t>::Fail(
			AudioErrorCode::DecodeFailed,
			"ma_decoder_read_pcm_frames failed (code=" + std::to_string(mr) + ")");
	}
	if (framesRead == 0) {
		return Result<std::uint64_t>::Ok(0);
	}
	if (framesRead < frameCount && mr != MA_AT_END) {
		mInfo.IsCorrupted = true;
	}

	const ma_uint64 samplesToConvert = framesRead * mInfo.Channels;
	ma_pcm_convert(outBuffer, reqFmt, nativeBuf.data(), nativeFmt, samplesToConvert,
				   ma_dither_mode_none);

	return Result<std::uint64_t>::Ok(framesRead);
}

Result<void> MiniaudioDecoder::SeekToFrame(std::uint64_t frameIndex)
{
	if (!mIsOpen || mDecoder == nullptr) {
		return Result<void>::Fail(AudioErrorCode::NotOpened);
	}
	if (mInfo.TotalFrames > 0 && frameIndex > mInfo.TotalFrames) {
		return Result<void>::Fail(AudioErrorCode::OutOfRange, "frameIndex > TotalFrames");
	}
	const ma_result mr = ma_decoder_seek_to_pcm_frame(mDecoder, frameIndex);
	if (mr != MA_SUCCESS) {
		mInfo.IsCorrupted = true;
		return Result<void>::Fail(
			AudioErrorCode::SeekFailed,
			"ma_decoder_seek_to_pcm_frame failed (code=" + std::to_string(mr) + ")");
	}
	return Result<void>::Ok();
}

std::uint64_t MiniaudioDecoder::GetPositionFrames() const
{
	if (!mIsOpen || mDecoder == nullptr)
		return 0;
	ma_uint64 cursor = 0;
	if (ma_decoder_get_cursor_in_pcm_frames(mDecoder, &cursor) != MA_SUCCESS) {
		return 0;
	}
	return cursor;
}

} // namespace audio_resover
