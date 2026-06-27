// utils::audio - C++17 音频处理库
// Umbrella 头文件：只需 include 这一个头文件即可拉入全部公开 API。
#pragma once

#include "utils/audio/AudioChannelConverter.hpp"
#include "utils/audio/AudioError.hpp"
#include "utils/audio/AudioFormat.hpp"
#include "utils/audio/AudioInfo.hpp"
#include "utils/audio/AudioReader.hpp"
#include "utils/audio/IAudioDecoder.hpp"
#include "utils/audio/IAudioResampler.hpp"

// 库版本
#define UTILS_AUDIO_VERSION_MAJOR 1
#define UTILS_AUDIO_VERSION_MINOR 0
#define UTILS_AUDIO_VERSION_PATCH 0
#define UTILS_AUDIO_VERSION_STRING "1.0.0"
