/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

// ============================================================================
// 转写流程集成测试
//
// 功能：读取 data 目录下的 01/02 音频文件，分别以 PARAFORMER / QWEN / HYBRID
//       三种模式进行转写，结果写入 data/cpp_output/，内容含声纹、时间戳、文字。
//
// 设计：
//   1. FileAudioSource 继承 GetAudioBase，按 1s 块流式推送 WAV 原始 PCM 字节，
//      模拟 1s 间隔的实时语音流
//   2. 三种模式统一走异步流式 Start 接口（线程内自动 PrepareTaskData + RunAasSynchronous
//      / HybridProcess），回调收集 AasResult
//   3. 框架侧在 RunAasSynchronous 前后用 steady_clock 计量纯处理耗时（VAD→文字），
//      写入 BmsInfo.mProcessLatencyMs；测试端回调直接读取，无需回查推送时刻
//   4. 推送完毕后 AasJobStop 触发收尾（OfflineLastRun + FlushHybridQwenBuffer），
//      等待所有结果回调后写入输出文件
//
// 输出文件命名：{音频序号}_{模式}.txt，如 01_Paraformer.txt
// 输出内容：段级时间戳、词级时间戳（QWEN/HYBRID 精确线）、说话人标签/名称/声纹MD5、
//           文字、每次回调的纯处理耗时；文件尾附 REALTIME/PRECISE 耗时统计
// ============================================================================

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "aas/aas_api.h"
#include "aas/aas_callback.h"
#include "aas/audio_load.h"
#include "common/config_manager.h"
#include "common/logger.h"

using namespace qifeng;
using namespace qifeng::aas;

namespace {

// ==================== 路径常量 ====================
const std::string DATA_DIR = "./data";
const std::string OUTPUT_DIR = "./data/cpp_output";
const std::string CONFIG_PATH = "./config/config.yaml";

// 每块推送时长（毫秒）：1s 一块，模拟真实实时语音流的推送间隔
constexpr int64_t CHUNK_MS = 1000;

using SteadyClock = std::chrono::steady_clock;

// ==================== WAV 文件解析 ====================
struct WavInfo {
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitDepth = 0;
    std::vector<uint8_t> pcm;  // data 段原始字节
};

// 解析 WAV 文件头，提取格式信息与 data 段 PCM 字节
bool ParseWav(const std::string& path, WavInfo& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        std::cerr << "[ERROR] 无法打开音频文件: " << path << std::endl;
        return false;
    }
    std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    if (raw.size() < 12) {
        std::cerr << "[ERROR] WAV 文件过小: " << path << std::endl;
        return false;
    }

    // 遍历 RIFF 子块，提取 fmt 格式信息与 data 段偏移
    for (size_t i = 12; i + 8 <= raw.size();) {
        const std::string chunkId = raw.substr(i, 4);
        uint32_t chunkSize = 0;
        std::memcpy(&chunkSize, &raw[i + 4], 4);

        if (chunkId == "fmt " && i + 8 + chunkSize <= raw.size()) {
            const size_t fmtBase = i + 8;
            std::memcpy(&out.channels, &raw[fmtBase + 2], 2);
            std::memcpy(&out.sampleRate, &raw[fmtBase + 4], 4);
            std::memcpy(&out.bitDepth, &raw[fmtBase + 14], 2);
        } else if (chunkId == "data") {
            const size_t dataOffset = i + 8;
            const size_t dataSize = std::min(static_cast<size_t>(chunkSize), raw.size() - dataOffset);
            out.pcm.assign(raw.begin() + static_cast<std::ptrdiff_t>(dataOffset),
                           raw.begin() + static_cast<std::ptrdiff_t>(dataOffset + dataSize));
            break;
        }
        i += 8 + chunkSize;
        if (chunkSize % 2 != 0) {
            i++;  // RIFF 块按 word 对齐，奇数尺寸需补 1 字节
        }
    }

    if (out.channels == 0 || out.sampleRate == 0 || out.bitDepth == 0 || out.pcm.empty()) {
        std::cerr << "[ERROR] WAV 头解析失败: ch=" << out.channels << " sr=" << out.sampleRate
                  << " bits=" << out.bitDepth << " pcmSize=" << out.pcm.size() << std::endl;
        return false;
    }
    return true;
}

// ==================== 流式音频源 ====================
// 继承 GetAudioBase，将 WAV data 段按 CHUNK_MS 切块逐次返回，模拟实时音频流
class FileAudioSource : public GetAudioBase {
public:
    FileAudioSource(const std::string& wavPath, int64_t chunkMs, ResultInfo::SendCallBack callback,
                   std::shared_ptr<SvDataBase> svDb, std::shared_ptr<HotWords> hotwords,
                   const std::string& sessionId)
        : mCallback(std::move(callback)),
          mSvDb(std::move(svDb)),
          mHotwords(std::move(hotwords)),
          mSessionId(sessionId) {
        if (!ParseWav(wavPath, mWav)) {
            mDone.store(true);
            return;
        }
        // 按块时长计算字节数：bytesPerMs = sampleRate * channels * (bitDepth/8) / 1000
        const size_t bytesPerSample = mWav.bitDepth / 8;
        const size_t frameSize = mWav.channels * bytesPerSample;
        // 统一转 int64_t 运算避免 unsigned/signed 混合，再转回 size_t
        mChunkBytes = static_cast<size_t>(static_cast<int64_t>(chunkMs) * static_cast<int64_t>(mWav.sampleRate)
                                          * static_cast<int64_t>(frameSize) / 1000);
        if (mChunkBytes == 0) {
            mChunkBytes = mWav.pcm.size();  // 防御：计算异常时整段返回
        }
        mOffset = 0;
        mStartTimeMs = 0;
        mTotalMs = static_cast<int64_t>(mWav.pcm.size()) * 1000
                   / (static_cast<int64_t>(mWav.sampleRate) * static_cast<int64_t>(frameSize));
        std::cout << "[INFO] 音频源就绪: " << wavPath << " (sr=" << mWav.sampleRate << " ch=" << mWav.channels
                  << " bits=" << mWav.bitDepth << " 总时长=" << mTotalMs << "ms)" << std::endl;
    }

    // 返回下一块音频；全部推送完毕返回 nullptr（AasThreadFunc 检测到后进入收尾）
    std::shared_ptr<ResultInfo> GetAudio(bool /*isNonblock*/) override {
        if (mOffset >= mWav.pcm.size()) {
            std::lock_guard<std::mutex> lock(mMutex);
            mDone.store(true);
            return nullptr;
        }

        const size_t end = std::min(mOffset + mChunkBytes, mWav.pcm.size());
        std::vector<uint8_t> chunk(mWav.pcm.begin() + static_cast<std::ptrdiff_t>(mOffset),
                                   mWav.pcm.begin() + static_cast<std::ptrdiff_t>(end));
        mOffset = end;

        // 块时长（毫秒）= 字节数 * 1000 / (sampleRate * channels * bytesPerSample)
        // 统一用 int64_t 运算避免 unsigned/signed 混合
        const int64_t frameSize = static_cast<int64_t>(mWav.channels) * (mWav.bitDepth / 8);
        const int64_t bytesPerSec = static_cast<int64_t>(mWav.sampleRate) * frameSize;
        const int64_t durationMs = static_cast<int64_t>(chunk.size()) * 1000 / bytesPerSec;

        // 构造 ResultInfo：mData 为原始 PCM 字节，mConfig 为原始格式，
        // AasThreadFunc 内部会通过 AudioLoad::GetPCMData 重采样到 16k/mono/float32
        auto info = std::shared_ptr<ResultInfo>(new ResultInfo{
            .mCallBack = mCallback,
            .mData = std::move(chunk),
            .mConfig = FormatConfig{
                .mSampleRate = mWav.sampleRate, .mChannels = mWav.channels, .mBitDepth = mWav.bitDepth},
            .mBmsInfo = BmsInfo{.mStartTime = mStartTimeMs,
                                .mEndTime = mStartTimeMs + durationMs,
                                .mAudioId = "",
                                .isOnline = false,
                                .mPushIntervalMs = durationMs},
            .mSvDataBase = mSvDb,
            .mHotwords = mHotwords,
            .mSessionId = mSessionId});

        mStartTimeMs += durationMs;
        // 进度提示（每 30s 音频一次）
        if (mStartTimeMs % 30000 < durationMs) {
            std::cout << "\r[INFO] 推送进度: " << mStartTimeMs << "/" << mTotalMs << "ms" << std::flush;
        }
        return info;
    }

    bool IsDone() const { return mDone.load(); }

private:
    WavInfo mWav;
    size_t mOffset = 0;
    size_t mChunkBytes = 0;
    int64_t mStartTimeMs = 0;
    int64_t mTotalMs = 0;
    ResultInfo::SendCallBack mCallback;
    std::shared_ptr<SvDataBase> mSvDb;
    std::shared_ptr<HotWords> mHotwords;
    std::string mSessionId;
    std::mutex mMutex;
    std::atomic<bool> mDone {false};
};

// ==================== 结果收集器 ====================
// 回调线程安全地收集所有 AasResult（HYBRID 模式下实时线/精确线多次回调），
// 同时记录每次回调的纯处理耗时（VAD→文字，由框架侧 steady_clock 计量后写入 BmsInfo）
struct CallbackStat {
    std::string source;      // REALTIME / PRECISE / UNKNOWN
    int64_t audioStartMs = 0;
    int64_t audioEndMs = 0;
    size_t segCount = 0;
    int64_t latencyMs = -1;  // 纯处理耗时（VAD→文字）；-1 = 未测量
};

struct ResultCollector {
    std::mutex mutex;
    std::vector<std::pair<AasResult, BmsInfo>> resultsWithBms;
    std::vector<CallbackStat> stats;

    void OnResult(AasResult result, BmsInfo bmsInfo) {
        std::lock_guard<std::mutex> lock(mutex);
        CallbackStat st;
        st.audioStartMs = bmsInfo.mStartTime;
        st.audioEndMs = bmsInfo.mEndTime;
        st.segCount = result.segments.size();
        st.latencyMs = bmsInfo.mProcessLatencyMs;
        // 来源以首个段的标记为准（无段时 UNKNOWN）
        if (!result.segments.empty()) {
            st.source = (result.segments[0].resultSource == AasResultSource::REALTIME) ? "REALTIME"
                                                                                        : "PRECISE";
        } else {
            st.source = "UNKNOWN";
        }
        stats.push_back(st);
        resultsWithBms.emplace_back(std::move(result), std::move(bmsInfo));
    }
};

// ==================== 输出文件写入 ====================
// 将毫秒时间戳格式化为 [mm:ss.SSS]
std::string FormatMs(int64_t ms) {
    const int64_t totalSec = ms / 1000;
    const int64_t minutes = totalSec / 60;
    const int64_t seconds = totalSec % 60;
    const int64_t millis = ms % 1000;
    std::ostringstream oss;
    oss << "[" << std::setfill('0') << std::setw(2) << minutes << ":" << std::setw(2) << seconds << "."
        << std::setw(3) << millis << "]";
    return oss.str();
}

// 来源标签（HYBRID 模式区分实时/精确结果）
std::string SourceLabel(AasResultSource src) {
    switch (src) {
        case AasResultSource::REALTIME: return "REALTIME(实时线)";
        case AasResultSource::PRECISE: return "PRECISE(精确线)";
        default: return "UNKNOWN";
    }
}

// 耗时分位数（vec 需已排序）
int64_t Percentile(std::vector<int64_t>& sorted, double p) {
    if (sorted.empty()) {
        return -1;
    }
    const size_t idx = std::min(sorted.size() - 1,
                                static_cast<size_t>(p / 100.0 * static_cast<double>(sorted.size())));
    return sorted[idx];
}

// 汇总某来源的耗时统计并写入 ofs
// 仅统计有输出段（segCount>0）的回调：滑动窗口增长阶段（仅 VAD、无输出）的耗时不代表
// "VAD→文字"处理能力，不计入
void WriteLatencySummary(std::ofstream& ofs, const std::string& source,
                         const std::vector<CallbackStat>& stats) {
    std::vector<int64_t> lat;
    for (const auto& s : stats) {
        if (s.source == source && s.segCount > 0 && s.latencyMs >= 0) {
            lat.push_back(s.latencyMs);
        }
    }
    if (lat.empty()) {
        ofs << "  " << (source.empty() ? "全部" : source) << ": 无耗时样本\n";
        return;
    }
    std::sort(lat.begin(), lat.end());
    const int64_t sum = std::accumulate(lat.begin(), lat.end(), static_cast<int64_t>(0));
    ofs << "  " << source << ": 样本=" << lat.size() << "  平均=" << sum / static_cast<int64_t>(lat.size())
        << "ms  P50=" << Percentile(lat, 50) << "ms  P95=" << Percentile(lat, 95)
        << "ms  最大=" << lat.back() << "ms\n";
}

// 将一个音频 + 一种模式的全部结果写入输出文件
void WriteResult(const std::string& audioLabel, const std::string& modeName,
                 const std::vector<std::pair<AasResult, BmsInfo>>& resultsWithBms,
                 const std::vector<CallbackStat>& stats) {
    const std::string outPath = OUTPUT_DIR + "/" + audioLabel + "_" + modeName + ".txt";
    std::ofstream ofs(outPath);
    if (!ofs.is_open()) {
        std::cerr << "[ERROR] 无法创建输出文件: " << outPath << std::endl;
        return;
    }

    // 统计总段数
    size_t totalSegs = 0;
    for (const auto& [res, bms] : resultsWithBms) {
        totalSegs += res.segments.size();
    }

    ofs << "============================================================\n"
        << "音频序号: " << audioLabel << "\n"
        << "转写模式: " << modeName << "\n"
        << "推送间隔: " << CHUNK_MS << "ms\n"
        << "回调次数: " << resultsWithBms.size() << "\n"
        << "总段数:   " << totalSegs << "\n"
        << "============================================================\n\n";

    // 推理耗时明细：每次回调一行
    ofs << "---------- 推理耗时明细 (VAD→文字 纯处理耗时) ----------\n";
    for (size_t i = 0; i < stats.size(); ++i) {
        const auto& s = stats[i];
        ofs << "  [回调 " << std::setw(3) << (i + 1) << "] " << std::setw(8) << s.source << "  音频区间 "
            << FormatMs(s.audioStartMs) << " - " << FormatMs(s.audioEndMs) << "  段数 " << s.segCount
            << "  耗时 " << (s.latencyMs >= 0 ? std::to_string(s.latencyMs) + "ms" : "N/A") << "\n";
    }
    ofs << "\n---------- 耗时统计 ----------\n";
    WriteLatencySummary(ofs, "REALTIME", stats);
    WriteLatencySummary(ofs, "PRECISE", stats);
    ofs << "----------------------------\n\n";

    int cbIdx = 0;
    for (const auto& [res, bms] : resultsWithBms) {
        ofs << "------------------------------------------------------------\n"
            << "[回调 " << (++cbIdx) << "] 时间区间: " << FormatMs(bms.mStartTime) << " - "
            << FormatMs(bms.mEndTime) << " (推送间隔 " << bms.mPushIntervalMs << "ms)\n"
            << "  返回码: " << res.code << " (" << res.message << ")\n"
            << "  段数: " << res.segments.size() << "\n"
            << "------------------------------------------------------------\n";

        for (size_t i = 0; i < res.segments.size(); ++i) {
            const auto& seg = res.segments[i];
            ofs << "\n  >>> 段 " << (i + 1) << " <<<\n"
                << "  段级时间戳: " << FormatMs(seg.startTime) << " - " << FormatMs(seg.endTime)
                << " (时长 " << (seg.endTime - seg.startTime) << "ms)\n"
                << "  声纹信息:\n"
                << "    说话人标签: " << seg.speakerLabel << "\n"
                << "    说话人名称: " << seg.speakerName << "\n"
                << "    声纹MD5:    " << seg.svEmbeddingMd5 << "\n"
                << "    声纹维度:   [" << seg.svEmbedding.size() << " x "
                << (seg.svEmbedding.empty() ? 0 : seg.svEmbedding[0].size()) << "]\n"
                << "  结果来源: " << SourceLabel(seg.resultSource) << "\n"
                << "  文字内容: " << seg.text << "\n";

            // 词级时间戳（QWEN / HYBRID 精确线输出；PARAFORMER 为空）
            if (!seg.timestamps.empty()) {
                ofs << "  词级时间戳:\n";
                for (const auto& ts : seg.timestamps) {
                    ofs << "    - " << ts.text << "  " << FormatMs(ts.startMs) << " - "
                        << FormatMs(ts.endMs) << "\n";
                }
            } else {
                ofs << "  词级时间戳: (无，当前模式未输出)\n";
            }
        }
        ofs << "\n";
    }

    ofs.close();
    std::cout << "\n[INFO] 结果已写入: " << outPath << " (回调 " << resultsWithBms.size()
              << " 次, 段 " << totalSegs << " 条)" << std::endl;
}

// ==================== 单次转写执行 ====================
struct TestMode {
    std::string name;
    AasArchType arch;
};

void RunTranscribe(const std::string& audioLabel, const std::string& wavPath, const TestMode& mode) {
    std::cout << "\n========== 开始转写: " << audioLabel << " | 模式: " << mode.name
              << " | 推送间隔 " << CHUNK_MS << "ms ==========" << std::endl;

    // 每轮独立的声纹数据库与热词（空），避免跨轮污染
    auto svDb = std::make_shared<SvDataBase>();
    auto hotwords = std::make_shared<HotWords>();

    // 结果收集器：回调线程安全收集
    auto collector = std::make_shared<ResultCollector>();
    auto callback = [collector](AasResult result, BmsInfo bmsInfo) {
        collector->OnResult(std::move(result), std::move(bmsInfo));
    };

    // 构造流式音频源：按 CHUNK_MS 切块推送
    auto source = std::make_shared<FileAudioSource>(wavPath, CHUNK_MS, callback, svDb, hotwords,
                                                    "session_" + audioLabel + "_" + mode.name);
    if (source->IsDone()) {
        std::cerr << "[ERROR] 音频源初始化失败，跳过" << std::endl;
        return;
    }

    const auto runStart = SteadyClock::now();

    // 启动异步流式转写（线程内自动 PrepareTaskData + DAG 执行 / HybridProcess）
    AasJobManager::GetInstance().Start(source, mode.arch, 1);

    // 等待音频源推送完毕
    while (!source->IsDone()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    // 额外等待：让最后一块音频处理完（实时线 paraformer 回调）
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // 停止转写：触发收尾（OfflineLastRun 处理缓存尾部 + HYBRID FlushHybridQwenBuffer），
    // 并等待所有在途精确线 future 完成
    AasJobStop();
    // 等待收尾回调完成
    std::this_thread::sleep_for(std::chrono::seconds(2));

    const auto runEnd = SteadyClock::now();
    const auto wallMs = std::chrono::duration_cast<std::chrono::milliseconds>(runEnd - runStart).count();

    // 写入结果
    {
        std::lock_guard<std::mutex> lock(collector->mutex);
        WriteResult(audioLabel, mode.name, collector->resultsWithBms, collector->stats);
    }

    // 控制台输出耗时概览
    {
        std::lock_guard<std::mutex> lock(collector->mutex);
        std::cout << "[STAT] " << audioLabel << " " << mode.name << " 总墙钟 " << wallMs << "ms, 回调 "
                  << collector->stats.size() << " 次" << std::endl;
        for (const auto& src : {"REALTIME", "PRECISE"}) {
            std::vector<int64_t> lat;
            for (const auto& s : collector->stats) {
                if (s.source == src && s.latencyMs >= 0) {
                    lat.push_back(s.latencyMs);
                }
            }
            if (!lat.empty()) {
                std::sort(lat.begin(), lat.end());
                const int64_t sum = std::accumulate(lat.begin(), lat.end(), static_cast<int64_t>(0));
                std::cout << "[STAT]   " << src << ": n=" << lat.size() << " avg=" << sum
                                 / static_cast<int64_t>(lat.size()) << "ms p50=" << Percentile(lat, 50)
                          << "ms p95=" << Percentile(lat, 95) << "ms max=" << lat.back() << "ms"
                          << std::endl;
            }
        }
    }

    // 重置状态：清除音频缓存与上下文，为下一轮准备（不卸载模型）
    AasJobReset();
    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::cout << "========== 转写完成: " << audioLabel << " | 模式: " << mode.name << " =========="
              << std::endl;
}

}  // namespace

// ==================== 主函数 ====================
int main(int argc, char* argv[]) {
    std::cout << "============ 转写流程集成测试 (1s 实时推送 + 耗时统计) ============" << std::endl;

    // 1. 初始化基础模块
    const bool configOk = ConfigManager::GetInstance().Initialize("flow_test", CONFIG_PATH, 3000);
    if (!configOk) {
        std::cerr << "[FATAL] ConfigManager 初始化失败" << std::endl;
        return -1;
    }
    const bool loggerOk = Logger::GetInstance().Initialize("flow_test");
    if (!loggerOk) {
        std::cerr << "[FATAL] Logger 初始化失败" << std::endl;
        return -1;
    }
    SLOG_INFO << "Flow test: 基础模块初始化完成";

    // 2. 加载模型（VAD/PUNC/SV/ASR/Qwen3-ASR/ForcedAligner）
    std::cout << "[INFO] 开始加载模型..." << std::endl;
    auto t0 = std::chrono::high_resolution_clock::now();
    if (InitializeAasResources() != 0) {
        std::cerr << "[FATAL] 模型加载失败" << std::endl;
        return -1;
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    std::cout << "[INFO] 模型加载完成，耗时 "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count() << "ms" << std::endl;

    // 3. 初始化 AAS 任务管理器
    AasJobManager::GetInstance().Initialize();

    // 4. 创建输出目录
    std::filesystem::create_directories(OUTPUT_DIR);

    // 5. 测试矩阵：2 个音频 x 3 种模式
    struct TestAudio {
        std::string label;
        std::string path;
    };
    const std::vector<TestAudio> audios = {
        {"01", DATA_DIR + "/01_诊断必备：手把手帮你快速解读临床常用化验单 - 第三节：血常规实战案例分析.wav"},
        {"02", DATA_DIR + "/02_医疗_肠道菌群.wav"},
    };
    const std::vector<TestMode> modes = {
        {"Paraformer", AasArchType::PARAFORMER},
        {"Qwen", AasArchType::QWEN},
        {"Hybrid", AasArchType::HYBRID},
    };

    // 6. 逐音频逐模式执行转写
    for (const auto& audio : audios) {
        for (const auto& mode : modes) {
            RunTranscribe(audio.label, audio.path, mode);
        }
    }

    // 7. 释放资源（先停止转写，再卸载模型）
    ReleaseAasResources();
    std::cout << "\n============ 测试全部完成 ============" << std::endl;
    std::cout << "结果输出目录: " << OUTPUT_DIR << std::endl;
    return 0;
}
