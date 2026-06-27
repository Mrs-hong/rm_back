// audio_resover - C++17 音频处理库
// 公开头文件：错误码 + Result<T> 返回类型。
//
// 所有公开 API 都返回 Result<T>，而不是抛异常。这样可以让本库适用于
// 禁用异常的嵌入式 / 边缘运行时，同时让错误传播在每个调用点都显式可见。
#pragma once

#include <string>
#include <utility>

namespace audio_resover
{

// 错误码命名空间分区：
//   1xxx - 文件 / IO
//   2xxx - 格式 / 解码
//   3xxx - 参数 / 状态
//   4xxx - 重采样
//   5xxx - 通道混合
//   9xxx - 内部 / 未实现
enum class AudioErrorCode : int {
	Ok = 0,

	// --- 文件 / IO ---
	FileNotFound = 1001,
	FileOpenFailed = 1002,
	FileReadFailed = 1003,

	// --- 格式 / 解码 ---
	UnknownFormat = 2001,
	UnsupportedFormat = 2002,
	CorruptedFile = 2003,
	DecodeFailed = 2004,
	SeekFailed = 2005,

	// --- 参数 / 状态 ---
	InvalidArgument = 3001,
	OutOfRange = 3002,
	NotOpened = 3003,
	AlreadyOpened = 3004,

	// --- 重采样 ---
	UnsupportedResampleDirection = 4001, // 尝试上采样（仅支持下采样）
	ResamplerNotSet = 4002,
	ResamplerInitFailed = 4003,
	ResampleFailed = 4004,

	// --- 通道混合 ---
	UnsupportedChannelDirection = 5001, // 不支持上混（target > source）
	ChannelMixFailed = 5002,			// 通道混合失败

	// --- 内部 ---
	InternalError = 9001,
	NotImplemented = 9002,
};

// 返回错误码对应的简短中文描述。
inline const char* ToMessage(AudioErrorCode code)
{
	switch (code) {
	case AudioErrorCode::Ok:
		return "Ok";
	// 文件 / IO
	case AudioErrorCode::FileNotFound:
		return "File not found";
	case AudioErrorCode::FileOpenFailed:
		return "Failed to open file";
	case AudioErrorCode::FileReadFailed:
		return "Failed to read file";
	// 格式 / 解码
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
	// 参数 / 状态
	case AudioErrorCode::InvalidArgument:
		return "Invalid argument";
	case AudioErrorCode::OutOfRange:
		return "Position out of range";
	case AudioErrorCode::NotOpened:
		return "Audio not opened";
	case AudioErrorCode::AlreadyOpened:
		return "Audio already opened";
	// 重采样
	case AudioErrorCode::UnsupportedResampleDirection:
		return "Only downsampling is supported";
	case AudioErrorCode::ResamplerNotSet:
		return "Resampler not configured";
	case AudioErrorCode::ResamplerInitFailed:
		return "Resampler init failed";
	case AudioErrorCode::ResampleFailed:
		return "Resample failed";
	// 通道混合
	case AudioErrorCode::UnsupportedChannelDirection:
		return "Only channel downmix is supported";
	case AudioErrorCode::ChannelMixFailed:
		return "Channel mix failed";
	// 内部
	case AudioErrorCode::InternalError:
		return "Internal error";
	case AudioErrorCode::NotImplemented:
		return "Not implemented";
	}
	return "Unknown error code";
}

// 泛型 Result<T>：要么持有值（Ok），要么持有错误码 + 消息。
// 对小尺寸 T 拷贝代价很低；对大尺寸 T 建议用 move 构造。
template <typename T> class Result
{
  public:
	// 构造一个持有默认值（Ok）的 Result。
	Result() = default;

	bool IsOk() const { return mCode == AudioErrorCode::Ok; }
	explicit operator bool() const { return IsOk(); }

	// 访问存储的值。在 !IsOk() 时调用为未定义行为。
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

// void 特化：只承载状态，不含值。
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
