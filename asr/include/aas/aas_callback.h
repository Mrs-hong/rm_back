/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_AAS_AAS_CALLBACK_H
#define QIFENG_FRAMEWORK_AAS_AAS_CALLBACK_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unistd.h>

namespace qifeng {
    namespace aas {

        // ==================== 数据结构定义 ====================

        // 架构类型
        enum class AasArchType {
            PARAFORMER,  // paraformer 架构（实时转写路线）
            QWEN,        // qwen 架构（带时间戳精确转写路线）
            HYBRID,      // 混合架构（paraformer 实时线 + qwen 累积线并行）
        };

        // 结果来源（混合路线下区分实时结果与精确结果，同一回调推送）
        enum class AasResultSource {
            REALTIME,  // paraformer 实时线结果（低延迟）
            PRECISE,   // qwen 累积线结果（带词级时间戳）
        };

        /**
         * @brief 词级时间戳项（对齐 python ForcedAlignItem：text/start_time/end_time）
         */
        struct AasTimestampItem {
            std::string text;     // token 文本
            int64_t startMs = 0;  // 绝对开始时间（毫秒）
            int64_t endMs = 0;    // 绝对结束时间（毫秒）
        };

        /**
         * @brief 语音转写大模型流式推理生成单次返回内容
         */
        struct StreamInferContent {
            std::string fixText;
            std::string unfixText;
            std::string sequence;
            bool isDecoded;
        };

        /**
         * @brief 音频片段信息
         */
        struct AasSegment {
            int64_t startTime;                            // 开始时间（秒）
            int64_t endTime;                              // 结束时间（秒）
            std::string text;                             // 文本内容
            int speakerLabel;                             // 说话人标签
            std::string speakerName;                      // 说话人姓名
            std::vector<std::vector<float>> svEmbedding;  // 声纹嵌入向量
            std::string svEmbeddingMd5;                   // 主要声纹嵌入MD5码
            bool isFullSegment = true;                    // 是否完整片段
            // 语音转写大模型流式推理生成单次返回内容
            StreamInferContent asrContent;
            // 词级时间戳（qwen 路径输出；绝对时间，含 BMS 起始偏移）
            std::vector<AasTimestampItem> timestamps;
            // 结果来源（混合路线下区分实时/精确结果）
            AasResultSource resultSource = AasResultSource::REALTIME;
        };

        /**
         * @brief AAS 执行结果
         */
        struct AasResult {
            int code = 0;                                 // 返回码（0=成功，其他=错误）
            std::string message;                          // 状态消息
            std::string traceId;                          // 追踪ID
            std::string text;                             // 完整识别文本
            int speakerLabel = -1;                        // 主要说话人标签
            std::vector<std::vector<float>> svEmbedding;  // 主要声纹嵌入
            std::string svEmbeddingMd5;                   // 主要声纹嵌入MD5码
            std::vector<AasSegment> segments;             // 分段信息

            // 内部使用：状态和时间戳（注意：ready使用普通bool，避免拷贝问题）
            bool ready = false;
            std::chrono::steady_clock::time_point submitTime;
        };

        struct FormatConfig {
            uint32_t mSampleRate = 16000;  // 采样率
            uint16_t mChannels = 1;        // 通道数
            uint16_t mBitDepth = 32;       // 位深
        };

        struct SvDataBase {
            std::unordered_map<std::string, std::vector<std::vector<float>>> svDatabase;
        };

        struct HotWords {
            std::vector<std::string> hotwords;
        };

        struct BmsInfo {
            int64_t mStartTime;
            int64_t mEndTime;
            std::string mAudioId;
            bool isOnline = true;  // 是否在线
            // 离线推送时间间隔
            int64_t mPushIntervalMs = 30000;
            // 纯处理耗时（VAD→文字，steady_clock），-1 = 未测量
            int64_t mProcessLatencyMs = -1;
        };

        struct ResultInfo {
            using SendCallBack = std::function<void(qifeng::aas::AasResult, qifeng::aas::BmsInfo)>;
            SendCallBack mCallBack;
            std::vector<uint8_t> mData;
            FormatConfig mConfig;
            BmsInfo mBmsInfo {};
            const std::shared_ptr<SvDataBase> mSvDataBase;
            const std::shared_ptr<HotWords> mHotwords;
            std::string mSessionId;
        };

        class GetAudioBase {
        public:
            GetAudioBase() = default;
            GetAudioBase(const GetAudioBase&) = default;
            GetAudioBase(GetAudioBase&&) = default;
            GetAudioBase& operator=(const GetAudioBase&) = default;
            GetAudioBase& operator=(GetAudioBase&&) = default;
            virtual ~GetAudioBase() = default;

            // isNonblock = true: 非阻塞，false：阻塞
            virtual std::shared_ptr<ResultInfo> GetAudio(bool isNonblock) = 0;
        };

        class GetAudioA : public GetAudioBase {
        public:
            GetAudioA() = default;
            GetAudioA(const GetAudioA&) = default;
            GetAudioA(GetAudioA&&) = default;
            GetAudioA& operator=(const GetAudioA&) = default;
            GetAudioA& operator=(GetAudioA&&) = default;
            ~GetAudioA() override = default;

            std::shared_ptr<ResultInfo> GetAudio(bool isNonblock) override {
                isNonblock = true;
                return nullptr;
            }
        };

        void AasParaformerJobStart(std::shared_ptr<GetAudioBase> getAudioBase,
                                   AasArchType archType = AasArchType::PARAFORMER, int numThreads = 1);

        void AasQwenJobStart(std::shared_ptr<GetAudioBase> getAudioBase, AasArchType archType = AasArchType::QWEN,
                             int numThreads = 1);

        // 混合路线启动：paraformer 实时线 + qwen 累积线并行，同一回调按 resultSource 区分
        void AasHybridJobStart(std::shared_ptr<GetAudioBase> getAudioBase, AasArchType archType = AasArchType::HYBRID,
                               int numThreads = 1);

        // 停止转写：不再接受新数据，等待所有在途结果处理完成后停止
        void AasJobStop();

        // 重置转写状态：清除音频缓存、说话人上下文、模型上下文（不卸载模型）
        void AasJobReset();

        int StartVoiceprintRegister(qifeng::aas::ResultInfo& info);

        // AAS资源的加载与释放（卸载模型：内部先停止转写，再释放所有模型资源）
        int InitializeAasResources();
        void ReleaseAasResources();
    }  // namespace aas
}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_AAS_AAS_CALLBACK_
