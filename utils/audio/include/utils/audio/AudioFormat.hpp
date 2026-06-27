// utils::audio - C++17 音频处理库
// 公开头文件：采样格式 / 容器格式枚举。
#pragma once

namespace utils::audio
{

// PCM 采样格式。决定每个采样在内存中的布局。
// F32 是推荐的输出格式（[-1.0, 1.0] 归一化）。
enum class SampleFormat : int {
	Unknown = 0,
	U8,	 // 8 位无符号 PCM，[0, 255]
	S16, // 16 位有符号 PCM
	S24, // 24 位有符号 PCM（紧凑布局，每个采样 3 字节）
	S32, // 32 位有符号 PCM
	F32, // 32 位 IEEE 浮点，[-1.0, 1.0]（ReadFrames 默认格式）
};

// 音频容器 / 编码格式。这是按文件内容嗅探得到的“真实”格式
// （与文件扩展名无关）。
enum class ContainerFormat : int {
	Unknown = 0,
	Wav,	// RIFF/WAVE（PCM、ADPCM、IEEE float 等）
	Mp3,	// MPEG audio layer III
	Flac,	// Free Lossless Audio Codec
	Vorbis, // Ogg Vorbis
	Raw,	// 无文件头的原始 PCM
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

} // namespace utils::audio
