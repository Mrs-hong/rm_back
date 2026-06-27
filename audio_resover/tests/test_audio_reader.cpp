// audio_resover - C++17 audio processing library
// Test suite (self-contained, no GoogleTest dependency).
//
// Usage:
//   ./test_audio_reader                  # uses compiled-in AUDIO_TEST_FILE
//   ./test_audio_reader /path/to/audio   # overrides test file path
//
// Exits 0 on success, non-zero on any failure.
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

// ---------- Test 1: open + basic metadata ----------
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
	CHECK(info.BitsPerSample == 16 || info.BitsPerSample == 32);		 // s16 or f32
	CHECK(info.DurationMs >= 3'500'000 && info.DurationMs <= 3'700'000); // ~1 hour
	CHECK(info.FileSizeBytes > 100'000'000ull);							 // > 100 MB
	CHECK(info.TotalFrames > 0);
}

// ---------- Test 2: real-format detection ignores extension ----------
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

// ---------- Test 3: streaming sequential read (first 30s only, for speed) ----------
void TestStreamingRead(const std::string& path)
{
	Section("Test 3: streaming read (first ~30s)");
	AudioReader reader;
	CHECK_OK(reader.Open(path));

	// Read first 30s = 30 * 44100 frames
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
		// Buffer size must match frames * channels
		CHECK_EQ(buf.size(), r.Value() * reader.GetInfo().Channels);
		totalRead += r.Value();
	}
	std::cout << "   read " << totalRead << " frames in ~30s window\n";
	CHECK(totalRead == wantFrames);
}

// ---------- Test 4: random time-range read [1ms, 23ms) ----------
void TestTimeRangeRead(const std::string& path)
{
	Section("Test 4: ReadTimeRangeMs(1, 23)");
	AudioReader reader;
	CHECK_OK(reader.Open(path));
	std::vector<float> samples;
	auto r = reader.ReadTimeRangeMs(1, 23, samples);
	CHECK_OK(r);
	const auto channels = reader.GetInfo().Channels;
	const std::uint64_t expectedFrames = (23ull - 1ull) * 44100 / 1000; // ~970
	// Tolerance of +-5 frames for boundary rounding
	CHECK(r.Value() >= expectedFrames - 5 && r.Value() <= expectedFrames + 5);
	CHECK_EQ(samples.size(), r.Value() * channels);
	std::cout << "   got " << r.Value() << " frames\n";
}

// ---------- Test 5: seek + read at 500ms ----------
void TestSeekAndRead(const std::string& path)
{
	Section("Test 5: SeekToMs(500) + read 10ms");
	AudioReader reader;
	CHECK_OK(reader.Open(path));
	CHECK_OK(reader.SeekToMs(500));
	std::vector<float> buf;
	// 10ms at 44100 Hz = 441 frames
	auto r = reader.ReadFrames(441, buf);
	CHECK_OK(r);
	CHECK(r.Value() == 441);
	const std::uint64_t posMs = reader.GetPositionMs();
	std::cout << "   position after read: " << posMs << " ms\n";
	// Allow +-3ms rounding tolerance.
	CHECK(posMs >= 507 && posMs <= 513);
}

// ---------- Test 6: resample down 44.1k -> 22.05k ----------
void TestDownsampling(const std::string& path)
{
	Section("Test 6: SetTargetSampleRate(22050)");
	AudioReader reader;
	CHECK_OK(reader.Open(path));
	CHECK_OK(reader.SetTargetSampleRate(22050));
	CHECK_EQ(reader.GetTargetSampleRate(), 22050u);

	// Read 100ms at 22050 Hz = 2205 frames
	std::vector<float> buf;
	auto r = reader.ReadFrames(2205, buf);
	CHECK_OK(r);
	// Linear resampler may produce a few frames +/- due to filter latency.
	CHECK(r.Value() >= 2200 && r.Value() <= 2210);
	std::cout << "   produced " << r.Value() << " output frames for 100ms at 22050 Hz\n";
}

// ---------- Test 7: upsampling must be rejected ----------
void TestUpsamplingRejected(const std::string& path)
{
	Section("Test 7: SetTargetSampleRate(96000) must fail");
	AudioReader reader;
	CHECK_OK(reader.Open(path));
	auto r = reader.SetTargetSampleRate(96000);
	CHECK_FAIL(r, AudioErrorCode::UnsupportedResampleDirection);
}

// ---------- Test 8: corrupted / truncated file ----------
void TestCorruptedFile(const std::string& path)
{
	Section("Test 8: truncated file (last 1KB removed)");
	// Build a truncated copy: original minus last 1024 bytes.
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
	// If Open succeeded, read the entire file - we should hit a premature EOF.
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
	// The truncated file should report fewer frames than the original.
	CHECK(total < reader.GetInfo().TotalFrames || reader.GetInfo().IsCorrupted);
	std::remove(tmpPath.c_str());
}

// ---------- Test 9: error code coverage ----------
void TestErrorCodes(const std::string& path)
{
	Section("Test 9: error codes");

	// 9a: open nonexistent file -> FileNotFound
	{
		AudioReader reader;
		auto r = reader.Open("/tmp/__definitely_does_not_exist__.xyz");
		CHECK_FAIL(r, AudioErrorCode::FileNotFound);
	}

	// 9b: ReadFrames before Open -> NotOpened
	{
		AudioReader reader;
		std::vector<float> buf;
		auto r = reader.ReadFrames(1024, buf);
		CHECK_FAIL(r, AudioErrorCode::NotOpened);
	}

	// 9c: SeekToMs before Open -> NotOpened
	{
		AudioReader reader;
		auto r = reader.SeekToMs(100);
		CHECK_FAIL(r, AudioErrorCode::NotOpened);
	}

	// 9d: SetTargetSampleRate before Open -> NotOpened
	{
		AudioReader reader;
		auto r = reader.SetTargetSampleRate(22050);
		CHECK_FAIL(r, AudioErrorCode::NotOpened);
	}

	// 9e: ReadTimeRangeMs with end<=start -> InvalidArgument
	{
		AudioReader reader;
		CHECK_OK(reader.Open(path));
		std::vector<float> buf;
		auto r = reader.ReadTimeRangeMs(50, 50, buf);
		CHECK_FAIL(r, AudioErrorCode::InvalidArgument);
	}
}

// ---------- Test 10: inject a custom (passthrough) resampler ----------
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
