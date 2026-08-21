/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_MODELS_HM_WORKER_ASR_WORKER_H
#define QIFENG_FRAMEWORK_INCLUDE_MODELS_HM_WORKER_ASR_WORKER_H

#include <any>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/config_manager.h"
#include "models_hm/worker/worker.h"

// ONNX Runtime前向声明
namespace Ort {
    class Env;
    class Session;
    class SessionOptions;
    class MemoryInfo;
    class AllocatorWithDefaultOptions;
    struct Value;
    struct RunOptions;
    class Exception;
}  // namespace Ort

namespace qifeng {

    class HotwordsWorker;

    /**
     * @class ASRWorker
     * @brief ASR模型工作者类 (TCIM 版本)，继承自ModelWorker基类
     * @note 使用std::any作为输入类型，std::string作为输出类型
     */
    class ASRWorker : public ModelWorker {
    public:
        /**
         * @brief 构造函数（多模型版本）
         * @param encoderPath encoder .hmm 模型路径（同时作为基类默认模型加载）
         * @param decoderPath decoder .hmm 模型路径（同时注册为 decoder 和 decoder_output 图）
         * @param tpuId TPU 设备 ID
         * @param ioMode IO 模式
         */
        ASRWorker(const std::string& encoderPath, const std::string& decoderPath, int tpuId = 0,
                  IOMode ioMode = IOMode::SYSIO);
        ~ASRWorker();

        std::string Inference(const std::vector<std::vector<float>>& speechFeat,
                              HotwordsWorker* hotwordsWorker = nullptr);

        /**
         * @brief 将字符串中的每个字符转换为其对应的ID
         * @param tokens 输入的UTF-8编码字符串vector
         * @return 字符ID的向量
         */
        std::vector<int> TokensToIds(const std::vector<std::string>& tokens);

    private:
        // 输入节点名称
        std::string mInNameSpeech;
        std::string mInNameSpeechLengths;
        std::vector<std::string> mInNamesCache;

        // 输出节点名称
        std::string mOutNameText;
        std::string mOutNameEncoderOut;
        std::string mOutNameEncoderOutLens;
        std::vector<std::string> mOutNamesCache;

        // 热词相关
        std::vector<std::string> mHotwords;
        bool mHotwordsEnabled;

        // 令牌转换器
        std::unordered_map<int, std::string> mTokenizer;
        std::unordered_map<std::string, int> mTokenToId;
        bool mTokenizerLoaded;

        // 特殊令牌ID
        int mBlankId;
        int mSosId;
        int mEosId;

        // ONNX Runtime会话管理
        std::unique_ptr<Ort::Env> mOnnxEnv;
        std::unique_ptr<Ort::Session> mPredictorSession;
        std::unique_ptr<Ort::SessionOptions> mSessionOptions;
        std::unique_ptr<Ort::MemoryInfo> mMemoryInfo;
        std::mutex mOnnxMutex;
        bool mOnnxInitialized;

        // 辅助方法
        void LoadTokenizer();
        std::vector<std::string> TokensToText(const std::vector<int>& tokens);
        std::string SentencePostprocess(const std::vector<std::string>& tokens);

        // ONNX推理方法
        struct InferenceResult {
            bool success;
            std::string message;
            double inferenceTimeMs;

            // 第一个输出：predict_logits
            std::vector<float> predictLogits;
            std::vector<int64_t> predictLogitsShape;

            // 第二个输出：token_num
            std::vector<float> tokenNum;
            std::vector<int64_t> tokenNumShape;
        };

        // 初始化ONNX Runtime
        bool InitOnnxRuntime();

        // 执行ONNX推理（使用持久化会话）
        InferenceResult RunOnnxInferencePersistent(const std::vector<float>& encodeLogitsData,
                                                   const std::vector<int64_t>& encodeLogitsShape,
                                                   const std::vector<float>& maskData,
                                                   const std::vector<int64_t>& maskShape);

        // 数据校验方法
        bool ValidateInputShape(const std::vector<int64_t>& shape, const std::string& shapeName);
        bool ValidateEncodeLogits(const std::vector<float>& data, const std::vector<int64_t>& shape);
        bool ValidateMaskData(const std::vector<float>& data, const std::vector<int64_t>& shape);

        // 清理ONNX资源
        void CleanupOnnxResources();

        // 调试追踪：将 encoder/decoder/predictor 的输入输出向量保存为 npy 文件
        // 用于和参考模型对比
        void InitTraceSaving();
        int mTraceSeq = -1;
        std::string mTraceDir;
    };

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_INCLUDE_MODELS_HM_WORKER_ASR_WORKER_H
