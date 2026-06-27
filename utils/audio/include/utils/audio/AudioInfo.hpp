// utils::audio - C++17 音频处理库
// 公开头文件：音频元数据 POD。
#pragma once

#include <cstdint>

#include "utils/audio/AudioFormat.hpp"

namespace utils::audio
{

// 已打开音频文件的聚合元数据。所有字段由 IAudioDecoder::Open 填充；
// 调用方应将其视为只读。
//
// 字段说明：
//   - IsValid:        文件成功解析，且至少能确定格式 / 采样率 / 通道数时为 true。
//   - IsCorrupted:    解码过程中检测到结构 / 同步 / CRC 错误时为 true。
//                     读取操作可能仍部分成功。
//   - DurationMs:     由 TotalFrames 与 SampleRate 推导；未知时为 0。
//   - AvgBitRateKbps: 压缩格式的平均码率；原始 PCM 为 0。
struct AudioInfo {
	bool IsValid = false;
	bool IsCorrupted = false;

	ContainerFormat Format = ContainerFormat::Unknown;
	SampleFormat NativeSampleFormat = SampleFormat::Unknown;

	std::uint32_t SampleRate = 0;	 // 采样率，单位 Hz
	std::uint32_t Channels = 0;		 // 通道数（1 = 单声道，2 = 立体声，...）
	std::uint32_t BitsPerSample = 0; // 每样本位数；压缩格式不适用时为 0

	std::uint64_t TotalFrames = 0; // 总帧数（1 帧 = 每通道 1 个采样）
	std::uint64_t DurationMs = 0;
	std::uint64_t FileSizeBytes = 0;

	std::uint32_t AvgBitRateKbps = 0;
};

} // namespace utils::audio
