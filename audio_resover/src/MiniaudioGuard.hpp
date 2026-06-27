// audio_resover - C++17 音频处理库
// 私有头文件：miniaudio RAII 守卫（扩展点）。
//
// miniaudio 的 decoder 与 data_converter API 在基于文件的解码场景下并不
// 需要全局上下文 (ma_context)，所以本守卫当前是空操作。保留它是作为
// 未来后端的扩展点（例如设备回放可能需要全局 init/teardown）。
#pragma once

namespace audio_resover
{

class MiniaudioGuard
{
  public:
	// 获取（未来）全局 miniaudio 资源。当前为空操作。
	MiniaudioGuard() = default;
	// 释放（未来）全局 miniaudio 资源。当前为空操作。
	~MiniaudioGuard() = default;

	MiniaudioGuard(const MiniaudioGuard&) = delete;
	MiniaudioGuard& operator=(const MiniaudioGuard&) = delete;
};

} // namespace audio_resover
