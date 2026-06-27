// audio_resover - C++17 音频处理库
// Umbrella 头文件：只需 include 这一个头文件即可拉入全部公开 API。
#pragma once

#include "AudioChannelConverter.hpp"
#include "AudioError.hpp"
#include "AudioFormat.hpp"
#include "AudioInfo.hpp"
#include "AudioReader.hpp"
#include "IAudioDecoder.hpp"
#include "IAudioResampler.hpp"

// 库版本
#define AUDIO_RESOVER_VERSION_MAJOR 1
#define AUDIO_RESOVER_VERSION_MINOR 0
#define AUDIO_RESOVER_VERSION_PATCH 0
#define AUDIO_RESOVER_VERSION_STRING "1.0.0"
