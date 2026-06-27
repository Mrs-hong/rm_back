// audio_resover - C++17 audio processing library
// Public header: error codes + Result<T> return type.
//
// All public APIs return Result<T> instead of throwing exceptions. This keeps
// the library suitable for embedded / edge runtimes where exceptions may be
// disabled, and makes error propagation explicit at every call site.
#pragma once

#include <string>
#include <utility>

namespace audio_resover
{

// Error code namespace partitioning:
//   1xxx - file / IO
//   2xxx - format / decode
//   3xxx - argument / state
//   4xxx - resampling
//   9xxx - internal / not implemented
enum class AudioErrorCode : int {
	Ok = 0,

	// --- File / IO ---
	FileNotFound = 1001,
	FileOpenFailed = 1002,
	FileReadFailed = 1003,

	// --- Format / decode ---
	UnknownFormat = 2001,
	UnsupportedFormat = 2002,
	CorruptedFile = 2003,
	DecodeFailed = 2004,
	SeekFailed = 2005,

	// --- Argument / state ---
	InvalidArgument = 3001,
	OutOfRange = 3002,
	NotOpened = 3003,
	AlreadyOpened = 3004,

	// --- Resampling ---
	UnsupportedResampleDirection = 4001, // attempted upsampling (only downsample supported)
	ResamplerNotSet = 4002,
	ResamplerInitFailed = 4003,
	ResampleFailed = 4004,

	// --- Internal ---
	InternalError = 9001,
	NotImplemented = 9002,
};

// Human-readable short description of an error code.
inline const char* ToMessage(AudioErrorCode code)
{
	switch (code) {
	case AudioErrorCode::Ok:
		return "Ok";
	// File / IO
	case AudioErrorCode::FileNotFound:
		return "File not found";
	case AudioErrorCode::FileOpenFailed:
		return "Failed to open file";
	case AudioErrorCode::FileReadFailed:
		return "Failed to read file";
	// Format / decode
	case AudioErrorCode::UnknownFormat:
		return "Unknown audio format";
	case AudioErrorCode::UnsupportedFormat:
		return "Unsupported audio format";
	case AudioErrorCode::CorruptedFile:
		return "Corrupted audio file";
	case AudioErrorCode::DecodeFailed:
		return "Decode failed";
	case AudioErrorCode::SeekFailed:
		return "Seek failed";
	// Argument / state
	case AudioErrorCode::InvalidArgument:
		return "Invalid argument";
	case AudioErrorCode::OutOfRange:
		return "Position out of range";
	case AudioErrorCode::NotOpened:
		return "Audio not opened";
	case AudioErrorCode::AlreadyOpened:
		return "Audio already opened";
	// Resampling
	case AudioErrorCode::UnsupportedResampleDirection:
		return "Only downsampling is supported";
	case AudioErrorCode::ResamplerNotSet:
		return "Resampler not configured";
	case AudioErrorCode::ResamplerInitFailed:
		return "Resampler init failed";
	case AudioErrorCode::ResampleFailed:
		return "Resample failed";
	// Internal
	case AudioErrorCode::InternalError:
		return "Internal error";
	case AudioErrorCode::NotImplemented:
		return "Not implemented";
	}
	return "Unknown error code";
}

// Generic Result<T>: carries either a value (Ok) or an error code + message.
// Designed to be cheap to copy for small T; for large T prefer move construction.
template <typename T> class Result
{
  public:
	// Constructs an Ok result holding a default-constructed value.
	Result() = default;

	bool IsOk() const { return mCode == AudioErrorCode::Ok; }
	explicit operator bool() const { return IsOk(); }

	// Access the stored value. Undefined behaviour if !IsOk().
	const T& Value() const { return mValue; }
	T& Value() { return mValue; }

	AudioErrorCode Code() const { return mCode; }
	const std::string& Message() const { return mMessage; }

	static Result Ok(T value)
	{
		Result r;
		r.mValue = std::move(value);
		r.mCode = AudioErrorCode::Ok;
		return r;
	}

	static Result Fail(AudioErrorCode code, std::string msg = "")
	{
		Result r;
		r.mCode = code;
		r.mMessage = std::move(msg);
		return r;
	}

  private:
	T mValue{};
	AudioErrorCode mCode = AudioErrorCode::Ok;
	std::string mMessage;
};

// Specialisation for void: carries only status, no value.
template <> class Result<void>
{
  public:
	Result() = default;

	bool IsOk() const { return mCode == AudioErrorCode::Ok; }
	explicit operator bool() const { return IsOk(); }

	AudioErrorCode Code() const { return mCode; }
	const std::string& Message() const { return mMessage; }

	static Result Ok() { return Result{}; }

	static Result Fail(AudioErrorCode code, std::string msg = "")
	{
		Result r;
		r.mCode = code;
		r.mMessage = std::move(msg);
		return r;
	}

  private:
	AudioErrorCode mCode = AudioErrorCode::Ok;
	std::string mMessage;
};

} // namespace audio_resover
