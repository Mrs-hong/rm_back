// audio_resover - C++17 音频处理库
// 测试套件（自包含，无 GoogleTest 依赖）。
//
// 用法：
//   ./test_audio_reader                  # 使用编译期默认的 AUDIO_TEST_FILE
//   ./test_audio_reader /path/to/audio   # 用 argv[1] 覆盖测试文件路径
//
// 退出码：成功为 0，任何失败均非 0。
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

#include "audio_resover/AudioResover.hpp"

using audio_resover::AudioErrorCode;
using audio_resover::AudioInfo;
using audio_resover::AudioReader;
using audio_resover::ContainerFormat;
using audio_resover::IAudioResampler;
using audio_resover::Result;
using audio_resover::SampleFormat;
using audio_resover::ToString;

namespace
{

int g_failures = 0;
int g_checks = 0;

void Check(bool cond, const char* expr, const char* file, int line)
{
	++g_checks;
	if (!cond) {
		std::cerr << "[FAIL] " << file << ":" << line << "  CHECK(" << expr << ")\n";
		++g_failures;
	}
}

#define CHECK(cond) Check((cond), #cond, __FILE__, __LINE__)

template <typename T>
void CheckEq(const T& a, const T& b, const char* ea, const char* eb, const char* file, int line)
{
	++g_checks;
	if (!(a == b)) {
		std::cerr << "[FAIL] " << file << ":" << line << "  CHECK_EQ(" << ea << ", " << eb << ")\n";
		if constexpr (std::is_enum_v<T>) {
			std::cerr << "        got a=" << static_cast<std::underlying_type_t<T>>(a)
					  << "  b=" << static_cast<std::underlying_type_t<T>>(b) << "\n";
		} else {
			std::cerr << "        got a=" << a << "  b=" << b << "\n";
		}
		++g_failures;
	}
}

#define CHECK_EQ(a, b) CheckEq((a), (b), #a, #b, __FILE__, __LINE__)

template <typename T> void CheckOk(const Result<T>& r, const char* expr, const char* file, int line)
{
	++g_checks;
	if (!r.IsOk()) {
		std::cerr << "[FAIL] " << file << ":" << line << "  expected Ok: " << expr
				  << "\n        code=" << static_cast<int>(r.Code()) << " ("
				  << audio_resover::ToMessage(r.Code()) << ")" << " msg=" << r.Message() << "\n";
		++g_failures;
	}
}

#define CHECK_OK(r) CheckOk((r), #r, __FILE__, __LINE__)

template <typename T>
void CheckFail(const Result<T>& r, AudioErrorCode expected, const char* expr, const char* file,
			   int line)
{
	++g_checks;
	if (r.IsOk()) {
		std::cerr << "[FAIL] " << file << ":" << line << "  expected error " << expr
				  << " but result was Ok\n";
		++g_failures;
	} else if (r.Code() != expected) {
		std::cerr << "[FAIL] " << file << ":" << line
				  << "  expected code=" << static_cast<int>(expected)
				  << " got=" << static_cast<int>(r.Code()) << " ("
				  << audio_resover::ToMessage(r.Code()) << ")\n";
		++g_failures;
	}
}

#define CHECK_FAIL(r, code) CheckFail((r), (code), #r, __FILE__, __LINE__)

void Section(const char* name)
{
	std::cout << "=== " << name << " ===\n";
}

// ---------- 测试 1：打开 + 基础元数据 ----------
void TestOpenAndInfo(const std::string& path)
{
	Section("Test 1: Open + GetInfo");
	AudioReader reader;
	CHECK_OK(reader.Open(path));
	CHECK(reader.IsOpen());

	const AudioInfo& info = reader.GetInfo();
	CHECK(info.IsValid);
	CHECK_EQ(info.Format, ContainerFormat::Mp3);
	CHECK_EQ(info.SampleRate, 44100u);
	CHECK_EQ(info.Channels, 2u);
	CHECK(info.NativeSampleFormat != SampleFormat::Unknown);
	CHECK(info.BitsPerSample == 16 || info.BitsPerSample == 32);		 // s16 或 f32
	CHECK(info.DurationMs >= 3'500'000 && info.DurationMs <= 3'700'000); // 约 1 小时
	CHECK(info.FileSizeBytes > 100'000'000ull);							 // > 100 MB
	CHECK(info.TotalFrames > 0);
}

// ---------- 测试 2：真实格式检测（忽略扩展名）----------
void TestRealFormatDetection(const std::string& path)
{
	Section("Test 2: real-format detection (.wav extension is MP3)");
	AudioReader reader;
	CHECK_OK(reader.Open(path));
	CHECK_EQ(reader.GetInfo().Format, ContainerFormat::Mp3);
	std::cout << "   Format = " << ToString(reader.GetInfo().Format) << "\n";
	std::cout << "   Native sample format = " << ToString(reader.GetInfo().NativeSampleFormat)
			  << "\n";
}

// ---------- 测试 3：流式顺序读取（仅前 30 秒，加速执行）----------
void TestStreamingRead(const std::string& path)
{
	Section("Test 3: streaming read (first ~30s)");
	AudioReader reader;
	CHECK_OK(reader.Open(path));

	// 读前 30 秒 = 30 * 44100 帧
	const std::uint64_t wantFrames = 30ull * 44100;
	std::uint64_t totalRead = 0;
	const std::uint64_t chunkSize = 4096;
	std::vector<float> buf;
	while (totalRead < wantFrames) {
		const std::uint64_t remaining = wantFrames - totalRead;
		const std::uint64_t thisChunk = std::min<std::uint64_t>(chunkSize, remaining);
		auto r = reader.ReadFrames(thisChunk, buf);
		CHECK_OK(r);
		if (r.Value() == 0)
			break; // EOF
		// 缓冲区大小必须等于 frames * channels
		CHECK_EQ(buf.size(), r.Value() * reader.GetInfo().Channels);
		totalRead += r.Value();
	}
	std::cout << "   read " << totalRead << " frames in ~30s window\n";
	CHECK(totalRead == wantFrames);
}

// ---------- 测试 4：随机时间区间读取 [1ms, 23ms) ----------
void TestTimeRangeRead(const std::string& path)
{
	Section("Test 4: ReadTimeRangeMs(1, 23)");
	AudioReader reader;
	CHECK_OK(reader.Open(path));
	std::vector<float> samples;
	auto r = reader.ReadTimeRangeMs(1, 23, samples);
	CHECK_OK(r);
	const auto channels = reader.GetInfo().Channels;
	const std::uint64_t expectedFrames = (23ull - 1ull) * 44100 / 1000; // 约 970
	// 边界取整容差 ±5 帧
	CHECK(r.Value() >= expectedFrames - 5 && r.Value() <= expectedFrames + 5);
	CHECK_EQ(samples.size(), r.Value() * channels);
	std::cout << "   got " << r.Value() << " frames\n";
}

// ---------- 测试 5：跳转 + 在 500ms 处读取 ----------
void TestSeekAndRead(const std::string& path)
{
	Section("Test 5: SeekToMs(500) + read 10ms");
	AudioReader reader;
	CHECK_OK(reader.Open(path));
	CHECK_OK(reader.SeekToMs(500));
	std::vector<float> buf;
	// 44100 Hz 下 10ms = 441 帧
	auto r = reader.ReadFrames(441, buf);
	CHECK_OK(r);
	CHECK(r.Value() == 441);
	const std::uint64_t posMs = reader.GetPositionMs();
	std::cout << "   position after read: " << posMs << " ms\n";
	// 允许 ±3ms 取整容差。
	CHECK(posMs >= 507 && posMs <= 513);
}

// ---------- 测试 6：下采样 44.1k -> 22.05k ----------
void TestDownsampling(const std::string& path)
{
	Section("Test 6: SetTargetSampleRate(22050)");
	AudioReader reader;
	CHECK_OK(reader.Open(path));
	CHECK_OK(reader.SetTargetSampleRate(22050));
	CHECK_EQ(reader.GetTargetSampleRate(), 22050u);

	// 22050 Hz 下读 100ms = 2205 帧
	std::vector<float> buf;
	auto r = reader.ReadFrames(2205, buf);
	CHECK_OK(r);
	// 线性重采样因滤波器延迟可能 ±几帧。
	CHECK(r.Value() >= 2200 && r.Value() <= 2210);
	std::cout << "   produced " << r.Value() << " output frames for 100ms at 22050 Hz\n";
}

// ---------- 测试 7：上采样必须被拒绝 ----------
void TestUpsamplingRejected(const std::string& path)
{
	Section("Test 7: SetTargetSampleRate(96000) must fail");
	AudioReader reader;
	CHECK_OK(reader.Open(path));
	auto r = reader.SetTargetSampleRate(96000);
	CHECK_FAIL(r, AudioErrorCode::UnsupportedResampleDirection);
}

// ---------- 测试 8：损坏 / 截断文件 ----------
void TestCorruptedFile(const std::string& path)
{
	Section("Test 8: truncated file (last 1KB removed)");
	// 构造截断副本：原文件去掉最后 1024 字节。
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		std::cerr << "   skipped (cannot open source)\n";
		return;
	}
	std::string tmpPath = "/tmp/audio_resover_truncated.mp3";
	std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
	if (!out) {
		std::cerr << "   skipped (cannot create temp file)\n";
		return;
	}
	in.seekg(0, std::ios::end);
	const std::streamoff sz = in.tellg();
	if (sz <= 1024) {
		std::cerr << "   skipped (source too small)\n";
		return;
	}
	const auto truncSize = static_cast<std::size_t>(sz - 1024);
	in.seekg(0, std::ios::beg);
	std::vector<char> copy(truncSize);
	in.read(copy.data(), static_cast<std::streamsize>(truncSize));
	out.write(copy.data(), static_cast<std::streamsize>(truncSize));
	in.close();
	out.close();

	AudioReader reader;
	auto openRes = reader.Open(tmpPath);
	if (!openRes.IsOk()) {
		std::cout << "   Open returned " << static_cast<int>(openRes.Code())
				  << " - acceptable (file is severely truncated)\n";
		return;
	}
	// 若 Open 成功，读完整个文件 —— 应当命中提前 EOF。
	std::vector<float> buf;
	std::uint64_t total = 0;
	while (true) {
		auto r = reader.ReadFrames(4096, buf);
		if (!r.IsOk())
			break;
		if (r.Value() == 0)
			break;
		total += r.Value();
	}
	std::cout << "   read " << total << " frames from truncated file; corrupted="
			  << (reader.GetInfo().IsCorrupted ? "yes" : "no") << "\n";
	// 截断文件读取的帧数应少于原文件总帧数。
	CHECK(total < reader.GetInfo().TotalFrames || reader.GetInfo().IsCorrupted);
	std::remove(tmpPath.c_str());
}

// ---------- 测试 9：错误码覆盖 ----------
void TestErrorCodes(const std::string& path)
{
	Section("Test 9: error codes");

	// 9a：打开不存在的文件 -> FileNotFound
	{
		AudioReader reader;
		auto r = reader.Open("/tmp/__definitely_does_not_exist__.xyz");
		CHECK_FAIL(r, AudioErrorCode::FileNotFound);
	}

	// 9b：Open 之前 ReadFrames -> NotOpened
	{
		AudioReader reader;
		std::vector<float> buf;
		auto r = reader.ReadFrames(1024, buf);
		CHECK_FAIL(r, AudioErrorCode::NotOpened);
	}

	// 9c：Open 之前 SeekToMs -> NotOpened
	{
		AudioReader reader;
		auto r = reader.SeekToMs(100);
		CHECK_FAIL(r, AudioErrorCode::NotOpened);
	}

	// 9d：Open 之前 SetTargetSampleRate -> NotOpened
	{
		AudioReader reader;
		auto r = reader.SetTargetSampleRate(22050);
		CHECK_FAIL(r, AudioErrorCode::NotOpened);
	}

	// 9e：ReadTimeRangeMs 传入 end<=start -> InvalidArgument
	{
		AudioReader reader;
		CHECK_OK(reader.Open(path));
		std::vector<float> buf;
		auto r = reader.ReadTimeRangeMs(50, 50, buf);
		CHECK_FAIL(r, AudioErrorCode::InvalidArgument);
	}
}

// ---------- 测试 10：注入自定义（passthrough）重采样器 ----------
class PassthroughResampler final : public IAudioResampler
{
  public:
	Result<void> Configure(std::uint32_t inSampleRate, std::uint32_t outSampleRate,
						   std::uint32_t channels, SampleFormat format) override
	{
		if (outSampleRate > inSampleRate) {
			return Result<void>::Fail(AudioErrorCode::UnsupportedResampleDirection,
									  "passthrough resampler: only downsampling supported");
		}
		mInRate = inSampleRate;
		mOutRate = outSampleRate;
		mChannels = channels;
		mBytesPerSample = (format == SampleFormat::F32) ? 4 : 0;
		mCalled = true;
		return Result<void>::Ok();
	}

	Result<std::uint64_t> Process(const void* inBuffer, std::uint64_t inFrameCount, void* outBuffer,
								  std::uint64_t outFrameCountCap) override
	{
		mProcessCalls++;
		const std::uint64_t outFrames = std::min(inFrameCount, outFrameCountCap);
		if (mBytesPerSample > 0) {
			std::memcpy(outBuffer, inBuffer,
						static_cast<std::size_t>(outFrames * mChannels * mBytesPerSample));
		}
		return Result<std::uint64_t>::Ok(outFrames);
	}

	std::uint64_t EstimateOutFrames(std::uint64_t inFrameCount) const override
	{
		return inFrameCount * mOutRate / mInRate;
	}

	bool mCalled = false;
	int mProcessCalls = 0;

  private:
	std::uint32_t mInRate = 0;
	std::uint32_t mOutRate = 0;
	std::uint32_t mChannels = 0;
	std::uint32_t mBytesPerSample = 0;
};

void TestCustomResampler(const std::string& path)
{
	Section("Test 10: inject custom passthrough resampler");
	AudioReader reader;
	CHECK_OK(reader.Open(path));
	auto res = std::make_unique<PassthroughResampler>();
	auto* raw = res.get();
	CHECK_OK(reader.SetResampler(std::move(res)));
	CHECK_OK(reader.SetTargetSampleRate(22050));
	CHECK(raw->mCalled);

	std::vector<float> buf;
	auto r = reader.ReadFrames(1024, buf);
	CHECK_OK(r);
	CHECK(raw->mProcessCalls > 0);
	std::cout << "   custom resampler invoked " << raw->mProcessCalls << " times\n";
}

} // namespace

int main(int argc, char** argv)
{
	std::string path;
	if (argc > 1) {
		path = argv[1];
	}
#ifdef AUDIO_TEST_FILE
	else {
		path = AUDIO_TEST_FILE;
	}
#endif

	if (path.empty()) {
		std::cerr << "Usage: " << argv[0] << " <audio-file>\n";
		return 2;
	}

	std::cout << "audio_resover test suite\n";
	std::cout << "Test file: " << path << "\n\n";

	TestOpenAndInfo(path);
	TestRealFormatDetection(path);
	TestStreamingRead(path);
	TestTimeRangeRead(path);
	TestSeekAndRead(path);
	TestDownsampling(path);
	TestUpsamplingRejected(path);
	TestCorruptedFile(path);
	TestErrorCodes(path);
	TestCustomResampler(path);

	std::cout << "\n================================\n";
	std::cout << "checks: " << g_checks << "  failures: " << g_failures << "\n";
	return g_failures == 0 ? 0 : 1;
}
