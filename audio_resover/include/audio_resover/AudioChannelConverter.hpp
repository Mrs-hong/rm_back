// audio_resover - C++17 音频处理库
// 公开头文件：通道混合策略。
//
// 当源音频是多通道（如立体声 2ch、5.1 等）而下游只接受单声道时，
// 需要做通道下混。ChannelMixMode 决定下混到单声道时的具体策略。
#pragma once

#include <cstdint>

#include "audio_resover/AudioFormat.hpp"

namespace audio_resover
{

// 通道混合策略（仅当 targetChannels == 1 且 sourceChannels > 1 时生效）。
enum class ChannelMixMode : int {
	Average = 0, // 等权平均（立体声→单声道标准做法，听感最自然）
	First = 1,	 // 只取第 0 通道（左声道，零拷贝 stride）
	Last = 2,	 // 只取第 N-1 通道（右声道，零拷贝 stride）
};

inline const char* ToString(ChannelMixMode m)
{
	switch (m) {
	case ChannelMixMode::Average:
		return "Average";
	case ChannelMixMode::First:
		return "First";
	case ChannelMixMode::Last:
		return "Last";
	}
	return "Unknown";
}

} // namespace audio_resover
