// utils::audio - C++17 音频处理库
// 私有头文件：基于 miniaudio 的 IAudioDecoder 实现。
#pragma once

#include <cstdint>
#include <string>

#include "utils/audio/AudioError.hpp"
#include "utils/audio/AudioFormat.hpp"
#include "utils/audio/AudioInfo.hpp"
#include "utils/audio/IAudioDecoder.hpp"

// 前向声明 miniaudio 类型，避免把 miniaudio 符号泄漏到公开 PIMPL 之外的
// 工程其它部分。
struct ma_decoder;

namespace utils::audio
{

class MiniaudioDecoder final : public IAudioDecoder
{
  public:
	MiniaudioDecoder();
	~MiniaudioDecoder() override;

	MiniaudioDecoder(const MiniaudioDecoder&) = delete;
	MiniaudioDecoder& operator=(const MiniaudioDecoder&) = delete;

	Result<void> Open(const std::string& filePath) override;
	bool IsOpen() const override;
	void Close() override;
	const AudioInfo& GetInfo() const override;
	Result<std::uint64_t> ReadFrames(std::uint64_t frameCount, SampleFormat outFormat,
									 void* outBuffer) override;
	Result<void> SeekToFrame(std::uint64_t frameIndex) override;
	std::uint64_t GetPositionFrames() const override;

  private:
	// 嗅探文件首部字节，判定其真实容器格式（与文件扩展名无关）。
	static ContainerFormat SniffContainerFormat(const std::string& filePath,
												std::uint64_t fileSize);

	// 分配并配置底层 ma_decoder。失败返回 false（由调用方翻译错误码）。
	bool InitMiniaudioDecoder(const std::string& filePath);

	ma_decoder* mDecoder = nullptr; // 持有所有权；在 Close()/析构中释放
	AudioInfo mInfo{};
	bool mIsOpen = false;
};

} // namespace utils::audio
