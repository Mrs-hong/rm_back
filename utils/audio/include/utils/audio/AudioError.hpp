// utils::audio - C++17 音频处理库
// 公开头文件:错误码 + 错误码描述。
//
// Result<T> 模板由公共模块 utils/common/Result.hpp 提供,本头文件仅定义
// utils::audio 域专用的 ErrorCode 枚举与 ToMessage 描述函数。
// 通过 using utils::Result 把模板引入到 utils::audio 域,
// 使本域代码与用户代码可直接使用非限定名 Result<T>(与原 audio_resover 风格一致)。
#pragma once

#include <string>
#include <utility>

#include "utils/common/Result.hpp"

namespace utils::audio
{

// 错误码命名空间分区:
//   1xxx - 文件 / IO
//   2xxx - 格式 / 解码
//   3xxx - 参数 / 状态
//   4xxx - 重采样
//   5xxx - 通道混合
//   9xxx - 内部 / 未实现
enum class ErrorCode : int {
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
	UnsupportedResampleDirection = 4001, // 尝试上采样(仅支持下采样)
	ResamplerNotSet = 4002,
	ResamplerInitFailed = 4003,
	ResampleFailed = 4004,

	// --- 通道混合 ---
	UnsupportedChannelDirection = 5001, // 不支持上混(target > source)
	ChannelMixFailed = 5002,			// 通道混合失败

	// --- 内部 ---
	InternalError = 9001,
	NotImplemented = 9002,
};

// 返回错误码对应的简短中文描述。
// 接受 int 而非 ErrorCode,便于直接接收 utils::Result<T>::Code() 的返回值;
// 内部 cast 回 ErrorCode 进行 switch。
inline const char* ToMessage(int code)
{
	switch (static_cast<ErrorCode>(code)) {
	case ErrorCode::Ok:
		return "Ok";
	// 文件 / IO
	case ErrorCode::FileNotFound:
		return "File not found";
	case ErrorCode::FileOpenFailed:
		return "Failed to open file";
	case ErrorCode::FileReadFailed:
		return "Failed to read file";
	// 格式 / 解码
	case ErrorCode::UnknownFormat:
		return "Unknown audio format";
	case ErrorCode::UnsupportedFormat:
		return "Unsupported audio format";
	case ErrorCode::CorruptedFile:
		return "Corrupted audio file";
	case ErrorCode::DecodeFailed:
		return "Decode failed";
	case ErrorCode::SeekFailed:
		return "Seek failed";
	// 参数 / 状态
	case ErrorCode::InvalidArgument:
		return "Invalid argument";
	case ErrorCode::OutOfRange:
		return "Position out of range";
	case ErrorCode::NotOpened:
		return "Audio not opened";
	case ErrorCode::AlreadyOpened:
		return "Audio already opened";
	// 重采样
	case ErrorCode::UnsupportedResampleDirection:
		return "Only downsampling is supported";
	case ErrorCode::ResamplerNotSet:
		return "Resampler not configured";
	case ErrorCode::ResamplerInitFailed:
		return "Resampler init failed";
	case ErrorCode::ResampleFailed:
		return "Resample failed";
	// 通道混合
	case ErrorCode::UnsupportedChannelDirection:
		return "Only channel downmix is supported";
	case ErrorCode::ChannelMixFailed:
		return "Channel mix failed";
	// 内部
	case ErrorCode::InternalError:
		return "Internal error";
	case ErrorCode::NotImplemented:
		return "Not implemented";
	}
	return "Unknown error code";
}

// 引入 utils::Result 模板到 utils::audio 域,使本域内可直接使用非限定名 Result<T>。
using utils::Result;

} // namespace utils::audio
