// audio_resover - C++17 音频处理库
// Umbrella 头文件：只需 include 这一个头文件即可拉入全部公开 API。
#pragma once

#include "audio_resover/AudioError.hpp"
#include "audio_resover/AudioFormat.hpp"
#include "audio_resover/AudioInfo.hpp"
#include "audio_resover/AudioReader.hpp"
#include "audio_resover/IAudioDecoder.hpp"
#include "audio_resover/IAudioResampler.hpp"

// 库版本
#define AUDIO_RESOVER_VERSION_MAJOR 1
#define AUDIO_RESOVER_VERSION_MINOR 0
#define AUDIO_RESOVER_VERSION_PATCH 0
#define AUDIO_RESOVER_VERSION_STRING "1.0.0"
