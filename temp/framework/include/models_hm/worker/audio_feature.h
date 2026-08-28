/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */
#ifndef NUMCPP_NO_USE_BOOST
#define NUMCPP_NO_USE_BOOST
#endif

#ifndef QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_AUDIO_FEATURE_H
#define QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_AUDIO_FEATURE_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

#include <NumCpp/Core.hpp>
#include <NumCpp/FFT.hpp>
#include <NumCpp/Functions.hpp>
#include <NumCpp/NdArray.hpp>
#include <NumCpp/Utils.hpp>

namespace qifeng {
    /**
     * @class AudioFeature
     * @brief 音频特征提取模块，提供与Python接口完全一致的C++实现
     *
     * 本模块提供与Python audio.py中功能完全一致的接口，包括：
     * - CMVN加载和应用
     * - LFR（帧级下采样）处理
     * - FBank特征提取
     * - 各种音频处理工具函数
     *
     * 特性：
     * - CMVN数据启动时加载与缓存机制
     * - 线程安全的静态数据管理
     * - 完整的错误处理和日志记录
     */
    class AudioFeature {
    public:
        /**
         * @brief 窗口类型枚举
         */
        enum class WindowType { HAMMING, HANNING, POVEY, RECTANGULAR, BLACKMAN };

        /**
         * @brief 初始化CMVN数据加载
         * @param configPath 配置文件路径
         * @return bool 初始化是否成功
         *
         * 在应用程序启动阶段调用此函数，从配置文件加载CMVN数据
         * 支持多线程环境下的安全初始化（双重检查锁定模式）
         */
        static bool InitializeCmvn();

        /**
         * @brief 释放CMVN数据资源
         *
         * 在应用程序关闭阶段调用此函数，释放缓存的CMVN数据
         */
        static void ReleaseCmvn();

        /**
         * @brief 加载CMVN（倒谱均值方差归一化）文件
         * @param cmvnFile CMVN文件路径
         * @return CMVN数据，形状为(2, dim)，其中dim为特征维度
         *
         * 与Python load_cmvn函数完全一致：
         * - 加载CMVN配置文件
         * - 解析均值和方差数据
         * - 返回NumCpp NdArray格式的CMVN数据
         */
        static nc::NdArray<float> LoadCmvn(const std::string& cmvnFile);

        /**
         * @brief 应用CMVN（倒谱均值方差归一化）
         * @param inputs 输入特征数据，形状为(n, dim)
         * @param cmvnType CMVN类型，可以是"asr"或"vad"
         * @return 归一化后的特征数据，形状为(n, dim)
         *
         * 使用缓存的CMVN数据进行归一化处理：
         * - 自动选择对应的CMVN数据（ASR或VAD）
         * - 并行处理输入特征
         * - 应用均值和方差归一化
         */
        static nc::NdArray<float> ApplyCmvn(const nc::NdArray<float>& inputs, const std::string& cmvnType = "asr");

        /**
         * @brief 应用LFR（帧级下采样）
         * @param inputs 输入特征数据，形状为(T, dim)
         * @param lfrM LFR窗口大小
         * @param lfrN LFR窗口步长
         * @return LFR处理后的特征数据
         *
         * 与Python apply_lfr函数完全一致：
         * - 对输入特征进行帧级下采样
         * - 支持边界处理
         * - 返回处理后的特征数据
         */
        static nc::NdArray<float> ApplyLfr(const nc::NdArray<float>& inputs, int lfrM, int lfrN);

        /**
         * @brief 提取FBank特征
         * @param waveform 输入波形数据
         * @param blackmanCoeff Blackman窗口系数，默认为0.42
         * @param dither 抖动参数，默认为0.0
         * @param frameLength 帧长（毫秒），默认为25.0
         * @param frameShift 帧移（毫秒），默认为10.0
         * @param highFreq 最高频率，默认为0.0（表示Nyquist频率）
         * @param lowFreq 最低频率，默认为20.0
         * @param numMelBins Mel滤波器数量，默认为23
         * @param preemphasisCoeff 预加重系数，默认为0.97
         * @param removeDcOffset 是否移除DC偏移，默认为true
         * @param roundToPowerOfTwo 是否将窗口大小舍入为2的幂，默认为true
         * @param sampleFrequency 采样频率，默认为16000.0
         * @param snipEdges 是否裁剪边缘，默认为true
         * @param useLogFbank 是否使用对数FBank，默认为true
         * @param usePower 是否使用功率谱，默认为true
         * @param vtlnHigh VTLN最高频率，默认为-500.0
         * @param vtlnLow VTLN最低频率，默认为100.0
         * @param vtlnWarp VTLN warp因子，默认为1.0
         * @param windowType 窗口类型，默认为POVEY
         * @return FBank特征数据
         *
         * 与Python fbank函数完全一致：
         * - 支持多种窗口类型
         * - 支持VTLN（vocal tract length normalization）
         * - 支持对数FBank和功率谱
         * - 内部使用kaldi-native-fbank库实现
         */
        static nc::NdArray<float> Fbank(const nc::NdArray<float>& waveform, float blackmanCoeff = 0.42f,
                                        float dither = 0.0f, float frameLength = 25.0f, float frameShift = 10.0f,
                                        float highFreq = 0.0f, float lowFreq = 20.0f, int numMelBins = 23,
                                        float preemphasisCoeff = 0.97f, bool removeDcOffset = true,
                                        bool roundToPowerOfTwo = true, float sampleFrequency = 16000.0f,
                                        bool snipEdges = true, bool useLogFbank = true, bool usePower = true,
                                        float vtlnHigh = -500.0f, float vtlnLow = 100.0f, float vtlnWarp = 1.0f,
                                        WindowType windowType = WindowType::POVEY);

        /**
         * @brief VAD特征提取
         * @param audioSample 输入音频波形（一维数组）
         * @return VAD特征数据（二维数组），空数组表示失败
         *
         * 与Python VadFeature函数完全一致：
         * - 计算FBank特征
         * - 应用LFR（帧级下采样，m=5, n=1）
         * - 应用CMVN（倒谱均值方差归一化，vad类型）
         * - 返回处理后的VAD特征
         */
        static std::vector<std::vector<float>> VadFeature(const std::vector<float>& audioSample);

        /**
         * @brief ASR特征提取
         * @param audioSample 输入音频波形（一维数组）
         * @return ASR特征数据（二维数组），空数组表示失败
         *
         * 与Python AsrFeature函数完全一致：
         * - 音频数据乘以(1 << 15)
         * - 计算FBank特征
         * - 应用LFR（帧级下采样，m=7, n=6）
         * - 应用CMVN（倒谱均值方差归一化，asr类型）
         * - 返回处理后的ASR特征
         */
        static std::vector<std::vector<float>> AsrFeature(const std::vector<float>& audioSample);

        /**
         * @brief VP特征提取
         * @param audioSample 输入音频波形（一维数组）
         * @return VP特征数据（二维数组），空数组表示失败
         *
         * 与Python VpFeature函数完全一致：
         * - 计算FBank特征
         * - 减去均值（按列计算）
         * - 返回处理后的VP特征
         */
        static std::vector<std::vector<float>> VpFeature(const std::vector<float>& audioSample);

    private:
        static constexpr float kEpsilon = 1e-8f;
        static constexpr float kMillisecondsToSeconds = 0.001f;
        static constexpr float kMelScaleFactor = 1127.0f;
        static constexpr float kMelScaleOffset = 700.0f;

        /**
         * @brief CMVN数据缓存结构
         */
        struct CmvnData {
            nc::NdArray<float> data;
            bool initialized = false;
            std::chrono::milliseconds loadTime {};
            std::string filePath;
        };

        static nc::NdArray<float> mAsrCmvnData;
        static nc::NdArray<float> mVadCmvnData;
        static std::mutex mCmvnMutex;
        static std::atomic<bool> mInitialized;

        /**
         * @brief 获取指定类型的CMVN数据（只读访问）
         * @param cmvnType CMVN类型，可以是"asr"或"vad"
         * @return 指向CMVN数据的const指针，未初始化则返回空
         */
        static nc::NdArray<float> GetCmvnData(const std::string& cmvnType);

        /**
         * @brief 读取YAML配置文件
         * @param configPath 配置文件路径
         * @param asrCmvPath 输出参数，ASR CMVN文件路径
         * @param vadCmvPath 输出参数，VAD CMVN文件路径
         * @return bool 读取是否成功
         */
        static bool ReadConfigFile(const std::string& configPath, std::string& asrCmvPath, std::string& vadCmvPath);

        /**
         * @brief 获取下一个2的幂
         * @param x 输入值
         * @return 下一个2的幂
         */
        static int NextPowerOf2(int x);

        /**
         * @brief 获取分帧后的音频数据
         * @param waveform 输入波形数据
         * @param windowSize 窗口大小
         * @param windowShift 窗口步长
         * @param snipEdges 是否裁剪边缘
         * @return 分帧后的音频数据，形状为(numFrames, windowSize)
         */
        static nc::NdArray<float> GetStrided(const nc::NdArray<float>& waveform, int windowSize, int windowShift,
                                             bool snipEdges);

        /**
         * @brief 获取窗口函数
         * @param windowType 窗口类型
         * @param windowSize 窗口大小
         * @param blackmanCoeff Blackman窗口系数
         * @return 窗口函数数据
         */
        static nc::NdArray<float> FeatureWindowFunction(WindowType windowType, int windowSize, float blackmanCoeff);

        /**
         * @brief 获取窗口属性
         * @param sampleFrequency 采样频率
         * @param frameShift 帧移（毫秒）
         * @param frameLength 帧长（毫秒）
         * @param roundToPowerOfTwo 是否将窗口大小舍入为2的幂
         * @return 窗口步长、窗口大小、填充后的窗口大小
         */
        static std::tuple<int, int, int> GetWindowProperties(float sampleFrequency, float frameShift, float frameLength,
                                                             bool roundToPowerOfTwo);

        /**
         * @brief 获取加窗后的音频数据
         * @param waveform 输入波形数据
         * @param paddedWindowSize 填充后的窗口大小
         * @param windowSize 窗口大小
         * @param windowShift 窗口步长
         * @param windowType 窗口类型
         * @param blackmanCoeff Blackman窗口系数
         * @param snipEdges 是否裁剪边缘
         * @param dither 抖动参数
         * @param removeDcOffset 是否移除DC偏移
         * @param preemphasisCoeff 预加重系数
         * @return 加窗后的音频数据，形状为(numFrames, paddedWindowSize)
         */
        static nc::NdArray<float> GetWindow(const nc::NdArray<float>& waveform, int paddedWindowSize, int windowSize,
                                            int windowShift, WindowType windowType, float blackmanCoeff, bool snipEdges,
                                            float dither, bool removeDcOffset, float preemphasisCoeff);

        /**
         * @brief Mel刻度转换（标量）
         * @param freq 频率值
         * @return Mel刻度值
         */
        static float MelScaleScalar(float freq);

        /**
         * @brief Mel刻度转换
         * @param freq 频率数组
         * @return Mel刻度数组
         */
        static nc::NdArray<float> MelScale(const nc::NdArray<float>& freq);

        /**
         * @brief 逆Mel刻度转换（标量）
         * @param melFreq Mel刻度值
         * @return 频率值
         */
        static float InverseMelScaleScalar(float melFreq);

        /**
         * @brief 逆Mel刻度转换
         * @param melFreq Mel刻度数组
         * @return 频率数组
         */
        static nc::NdArray<float> InverseMelScale(const nc::NdArray<float>& melFreq);

        /**
         * @brief VTLN频率扭曲
         * @param vtlnLowCutoff VTLN低截止频率
         * @param vtlnHighCutoff VTLN高截止频率
         * @param lowFreq 最低频率
         * @param highFreq 最高频率
         * @param vtlnWarpFactor VTLN warp因子
         * @param freq 频率数组
         * @return 扭曲后的频率数组
         */
        static nc::NdArray<float> VtlnWarpFreq(float vtlnLowCutoff, float vtlnHighCutoff, float lowFreq, float highFreq,
                                               float vtlnWarpFactor, const nc::NdArray<float>& freq);

        /**
         * @brief VTLN Mel频率扭曲
         * @param vtlnLowCutoff VTLN低截止频率
         * @param vtlnHighCutoff VTLN高截止频率
         * @param lowFreq 最低频率
         * @param highFreq 最高频率
         * @param vtlnWarpFactor VTLN warp因子
         * @param melFreq Mel频率数组
         * @return 扭曲后的Mel频率数组
         */
        static nc::NdArray<float> VtlnWarpMelFreq(float vtlnLowCutoff, float vtlnHighCutoff, float lowFreq,
                                                  float highFreq, float vtlnWarpFactor,
                                                  const nc::NdArray<float>& melFreq);

        /**
         * @brief 获取Mel滤波器组
         * @param numBins Mel滤波器数量
         * @param windowLengthPadded 填充后的窗口大小
         * @param sampleFreq 采样频率
         * @param lowFreq 最低频率
         * @param highFreq 最高频率
         * @param vtlnLow VTLN最低频率
         * @param vtlnHigh VTLN最高频率
         * @param vtlnWarpFactor VTLN warp因子
         * @return Mel滤波器组
         */
        static nc::NdArray<float> GetMelBanks(int numBins, int windowLengthPadded, float sampleFreq, float lowFreq,
                                              float highFreq, float vtlnLow, float vtlnHigh, float vtlnWarpFactor);

        /**
         * @brief 将NumCpp NdArray转换为std::vector<std::vector<float>>
         * @param ndArray NumCpp NdArray对象
         * @return 转换后的二维向量
         */
        static std::vector<std::vector<float>> NdArrayToFloatMatrix(const nc::NdArray<float>& ndArray);

        /**
         * @brief 将std::vector<float>转换为NumCpp NdArray
         * @param vec 输入向量
         * @return 转换后的NdArray，形状为(n, 1)
         */
        static nc::NdArray<float> FloatVectorToNdArray(const std::vector<float>& vec);
    };

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_AUDIO_FEATURE_H