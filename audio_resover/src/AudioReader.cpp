// audio_resover - C++17 音频处理库
// 实现文件：AudioReader facade。
#include "audio_resover/AudioReader.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>

#include "MiniaudioDecoder.hpp"
#include "MiniaudioResampler.hpp"

namespace audio_resover
{

AudioReader::AudioReader() = default;

AudioReader::~AudioReader() = default;

AudioReader::AudioReader(AudioReader&&) noexcept = default;
AudioReader& AudioReader::operator=(AudioReader&&) noexcept = default;

Result<void> AudioReader::Open(const std::string& filePath)
{
	if (!mDecoder) {
		mDecoder = std::make_unique<MiniaudioDecoder>();
	}
	auto r = mDecoder->Open(filePath);
	if (!r) {
		return r;
	}
	// 重新打开时，重置已有的重采样目标。
	if (mResampler && mTargetSampleRate != 0) {
		auto rc = ReconfigureResampler();
		if (!rc)
			return rc;
	}
	return Result<void>::Ok();
}

Result<void> AudioReader::SetDecoder(std::unique_ptr<IAudioDecoder> decoder)
{
	if (mDecoder && mDecoder->IsOpen()) {
		return Result<void>::Fail(AudioErrorCode::AlreadyOpened,
								  "cannot replace decoder while open");
	}
	mDecoder = std::move(decoder);
	return Result<void>::Ok();
}

bool AudioReader::IsOpen() const
{
	return mDecoder && mDecoder->IsOpen();
}

void AudioReader::Close()
{
	if (mDecoder)
		mDecoder->Close();
	mResampler.reset();
	mTargetSampleRate = 0;
	mUsingDefaultResampler = false;
}

const AudioInfo& AudioReader::GetInfo() const
{
	static const AudioInfo kEmpty{};
	return mDecoder ? mDecoder->GetInfo() : kEmpty;
}

// --- 顺序读取 ---

Result<std::uint64_t> AudioReader::ReadFrames(std::uint64_t frameCount,
											  std::vector<float>& outSamples)
{
	outSamples.clear();
	if (!IsOpen()) {
		return Result<std::uint64_t>::Fail(AudioErrorCode::NotOpened);
	}

	const auto channels = GetInfo().Channels;
	if (channels == 0) {
		return Result<std::uint64_t>::Fail(AudioErrorCode::InternalError,
										   "channels == 0 (file not parsed?)");
	}

	if (mTargetSampleRate == 0 || mResampler == nullptr) {
		// 直通路径：从解码器直接读 F32 帧。
		outSamples.resize(static_cast<std::size_t>(frameCount * channels));
		auto r = mDecoder->ReadFrames(frameCount, SampleFormat::F32, outSamples.data());
		if (!r)
			return r;
		outSamples.resize(static_cast<std::size_t>(r.Value() * channels));
		return r;
	}

	// 重采样路径。
	const std::uint32_t srcRate = GetInfo().SampleRate;
	const std::uint32_t dstRate = mTargetSampleRate;
	if (srcRate == 0 || dstRate == 0) {
		return Result<std::uint64_t>::Fail(AudioErrorCode::InternalError, "invalid sample rate");
	}

	// 估算产出 frameCount 个输出帧需要多少输入帧，
	// 加少量 slack 以吸收滤波器延迟。
	const std::uint64_t inputEstimate = frameCount * srcRate / dstRate + 32;

	std::vector<float> inBuf(static_cast<std::size_t>(inputEstimate * channels));
	auto readRes = mDecoder->ReadFrames(inputEstimate, SampleFormat::F32, inBuf.data());
	if (!readRes)
		return readRes;
	const std::uint64_t inFrames = readRes.Value();
	if (inFrames == 0) {
		outSamples.clear();
		return Result<std::uint64_t>::Ok(0);
	}
	inBuf.resize(static_cast<std::size_t>(inFrames * channels));

	outSamples.resize(static_cast<std::size_t>(frameCount * channels));
	auto procRes = mResampler->Process(inBuf.data(), inFrames, outSamples.data(), frameCount);
	if (!procRes)
		return procRes;
	outSamples.resize(static_cast<std::size_t>(procRes.Value() * channels));
	return procRes;
}

Result<std::uint64_t> AudioReader::ReadFrames(std::uint64_t frameCount, SampleFormat fmt,
											  std::vector<std::uint8_t>& outBytes)
{
	outBytes.clear();
	if (!IsOpen()) {
		return Result<std::uint64_t>::Fail(AudioErrorCode::NotOpened);
	}
	if (fmt == SampleFormat::Unknown) {
		return Result<std::uint64_t>::Fail(AudioErrorCode::InvalidArgument,
										   "fmt must not be Unknown");
	}

	const auto channels = GetInfo().Channels;
	// 重采样目前仅在 F32 重载上支持；对于任意格式，调用方必须先关闭重采样。
	// 这样保持实现简单，也符合典型用法（F32 流式 + 偶尔的原始 int 提取）。
	if (mTargetSampleRate != 0 && mResampler != nullptr) {
		return Result<std::uint64_t>::Fail(AudioErrorCode::InvalidArgument,
										   "byte-overload ReadFrames does not "
										   "support active resampling; use the "
										   "F32 overload or clear resampler");
	}

	const std::size_t sampleSize = [fmt]() -> std::size_t {
		switch (fmt) {
		case SampleFormat::U8:
			return 1;
		case SampleFormat::S16:
			return 2;
		case SampleFormat::S24:
			return 3;
		case SampleFormat::S32:
			return 4;
		case SampleFormat::F32:
			return 4;
		case SampleFormat::Unknown:
			return 0;
		}
		return 0;
	}();
	if (sampleSize == 0) {
		return Result<std::uint64_t>::Fail(AudioErrorCode::InvalidArgument,
										   "unsupported sample format");
	}

	outBytes.resize(static_cast<std::size_t>(frameCount * channels * sampleSize));
	auto r = mDecoder->ReadFrames(frameCount, fmt, outBytes.data());
	if (!r)
		return r;
	outBytes.resize(static_cast<std::size_t>(r.Value() * channels * sampleSize));
	return r;
}

Result<std::uint64_t> AudioReader::ReadTimeRangeMs(std::uint64_t startMs, std::uint64_t endMs,
												   std::vector<float>& outSamples)
{
	outSamples.clear();
	if (!IsOpen()) {
		return Result<std::uint64_t>::Fail(AudioErrorCode::NotOpened);
	}
	if (endMs <= startMs) {
		return Result<std::uint64_t>::Fail(AudioErrorCode::InvalidArgument,
										   "endMs must be > startMs");
	}

	const auto& info = GetInfo();
	if (info.SampleRate == 0) {
		return Result<std::uint64_t>::Fail(AudioErrorCode::InternalError, "sample rate is 0");
	}

	// [startMs, endMs) 窗口对应的源采样率帧索引。
	const std::uint64_t startFrame = startMs * info.SampleRate / 1000ULL;
	const std::uint64_t endFrame = endMs * info.SampleRate / 1000ULL;
	if (startFrame >= endFrame) {
		return Result<std::uint64_t>::Ok(0);
	}
	if (info.TotalFrames > 0 && startFrame >= info.TotalFrames) {
		return Result<std::uint64_t>::Fail(AudioErrorCode::OutOfRange,
										   "startMs beyond end of file");
	}

	auto seekRes = mDecoder->SeekToFrame(startFrame);
	if (!seekRes) {
		return Result<std::uint64_t>::Fail(seekRes.Code(), seekRes.Message());
	}

	const std::uint64_t wantFrames = endFrame - startFrame;
	return ReadFrames(wantFrames, outSamples);
}

// --- 位置 / 跳转 ---

Result<void> AudioReader::SeekToMs(std::uint64_t ms)
{
	if (!IsOpen()) {
		return Result<void>::Fail(AudioErrorCode::NotOpened);
	}
	const auto& info = GetInfo();
	if (info.SampleRate == 0) {
		return Result<void>::Fail(AudioErrorCode::InternalError, "sample rate is 0");
	}
	const std::uint64_t frame = ms * info.SampleRate / 1000ULL;
	return mDecoder->SeekToFrame(frame);
}

Result<void> AudioReader::SeekToFrame(std::uint64_t frameIndex)
{
	if (!IsOpen()) {
		return Result<void>::Fail(AudioErrorCode::NotOpened);
	}
	return mDecoder->SeekToFrame(frameIndex);
}

std::uint64_t AudioReader::GetPositionMs() const
{
	if (!IsOpen())
		return 0;
	const auto& info = GetInfo();
	if (info.SampleRate == 0)
		return 0;
	return mDecoder->GetPositionFrames() * 1000ULL / info.SampleRate;
}

std::uint64_t AudioReader::GetPositionFrames() const
{
	if (!IsOpen())
		return 0;
	// 当重采样激活时，位置以“输出帧”单位表示，从而满足
	// GetPositionFrames() * GetTargetSampleRate() == GetPositionMs() * 1000。
	const std::uint64_t inPos = mDecoder->GetPositionFrames();
	if (mTargetSampleRate == 0 || mResampler == nullptr)
		return inPos;
	const auto srcRate = GetInfo().SampleRate;
	if (srcRate == 0)
		return inPos;
	return inPos * mTargetSampleRate / srcRate;
}

bool AudioReader::HasMore() const
{
	if (!IsOpen())
		return false;
	const auto& info = GetInfo();
	if (info.TotalFrames == 0)
		return true; // 长度未知：假定还有
	return GetPositionFrames() < info.TotalFrames;
}

// --- 重采样器管理 ---

Result<void> AudioReader::SetResampler(std::unique_ptr<IAudioResampler> resampler)
{
	mResampler = std::move(resampler);
	mUsingDefaultResampler = false;
	if (mResampler == nullptr) {
		// 已清除：同时重置目标采样率。
		mTargetSampleRate = 0;
		return Result<void>::Ok();
	}
	if (IsOpen() && mTargetSampleRate != 0) {
		return ReconfigureResampler();
	}
	return Result<void>::Ok();
}

Result<void> AudioReader::SetTargetSampleRate(std::uint32_t targetSampleRate)
{
	if (!IsOpen()) {
		return Result<void>::Fail(AudioErrorCode::NotOpened, "Open() before SetTargetSampleRate()");
	}
	const auto srcRate = GetInfo().SampleRate;
	if (targetSampleRate == 0) {
		// 重置为原始采样率。
		mResampler.reset();
		mTargetSampleRate = 0;
		mUsingDefaultResampler = false;
		return Result<void>::Ok();
	}
	if (targetSampleRate > srcRate) {
		return Result<void>::Fail(AudioErrorCode::UnsupportedResampleDirection,
								  "target sample rate (" + std::to_string(targetSampleRate) +
									  ") > source (" + std::to_string(srcRate) +
									  "); upsampling not supported");
	}
	if (targetSampleRate == srcRate) {
		// 空操作：清除重采样器。
		mResampler.reset();
		mTargetSampleRate = 0;
		mUsingDefaultResampler = false;
		return Result<void>::Ok();
	}
	if (!mResampler) {
		mResampler = std::make_unique<MiniaudioResampler>();
		mUsingDefaultResampler = true;
	}
	mTargetSampleRate = targetSampleRate;
	return ReconfigureResampler();
}

std::uint32_t AudioReader::GetTargetSampleRate() const
{
	return mTargetSampleRate;
}

Result<void> AudioReader::ReconfigureResampler()
{
	if (!mResampler)
		return Result<void>::Ok();
	if (!IsOpen()) {
		return Result<void>::Fail(AudioErrorCode::NotOpened);
	}
	const auto& info = GetInfo();
	return mResampler->Configure(info.SampleRate, mTargetSampleRate, info.Channels,
								 SampleFormat::F32);
}

} // namespace audio_resover
