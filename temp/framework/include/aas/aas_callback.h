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

#include "common/utils/status.h"

namespace qifeng {
    namespace aas {

        // ==================== 数据结构定义 ====================

        // 架构类型
        enum class AasArchType {
            PARAFORMER,  // paraformer 架构
            QWEN,        // qwen 架构
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

        void AasJobStop();

        int StartVoiceprintRegister(qifeng::aas::ResultInfo& info);

        // AAS资源的加载与释放
        int InitializeAasResources();
        void ReleaseAasResources();

        // 模型完整性校验状态查询（返回通用 Status：code!=0 表示损坏，msg 携带详情）
        qifeng::Status GetModelVerifyStatus();
    }  // namespace aas
}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_AAS_AAS_CALLBACK_
