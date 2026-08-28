/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef NUMCPP_NO_USE_BOOST
#define NUMCPP_NO_USE_BOOST
#endif

#ifndef QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_WORKER_H
#define QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_WORKER_H

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "common/logger.h"
#include "define.h"
#include "error.h"
#include "tensor_pool.h"

#include "bmdef.h"
#include "bmlib_runtime.h"
#include "bmruntime_interface.h"

namespace qifeng {

    using FloatMatrix = std::vector<std::vector<float>>;

    /**
     * @enum IOMode
     * @brief IO模式枚举，用于指定推理时的数据搬运方式
     */
    enum class IOMode {
        SYSIO,   ///< 系统内存IO模式（通过bm_memcpy_s2d/d2s搬运数据）
        TPUOSIO  ///< TPU IO模式（保留，当前实现均走SYSIO路径）
    };

    /**
     * @struct ModelInput
     * @brief 模型输入数据结构
     */
    struct ModelInput {
        std::map<std::string, void*> data;
        std::map<std::string, std::vector<int>> shapes;
    };

    /**
     * @struct ModelOutput
     * @brief 模型输出数据结构
     */
    struct ModelOutput {
        std::map<std::string, void*> data;
        std::map<std::string, std::vector<int>> shapes;
    };

    /**
     * @class Flattener
     * @brief 高维数组展平工具类
     * @tparam T 元素类型
     */
    template <typename T>
    class Flattener {
    public:
        /**
         * @brief 将二维数组展平为一维数组
         * @param input 二维输入数组
         * @return 展平后的一维数组
         */
        static std::vector<T> Flatten(const std::vector<std::vector<T>>& input) {
            size_t totalSize = 0;
            for (const auto& row : input) {
                totalSize += row.size();
            }

            std::vector<T> output;
            output.reserve(totalSize);

            for (const auto& row : input) {
                output.insert(output.end(), row.begin(), row.end());
            }
            return output;
        }

        /**
         * @brief 将三维数组展平为一维数组
         * @param input 三维输入数组
         * @return 展平后的一维数组
         */
        static std::vector<T> Flatten(const std::vector<std::vector<std::vector<T>>>& input) {
            size_t totalSize = 0;
            for (const auto& matrix : input) {
                for (const auto& row : matrix) {
                    totalSize += row.size();
                }
            }

            std::vector<T> output;
            output.reserve(totalSize);

            for (const auto& matrix : input) {
                for (const auto& row : matrix) {
                    output.insert(output.end(), row.begin(), row.end());
                }
            }
            return output;
        }
    };

    /**
     * @class Restorer
     * @brief 高维数组恢复工具类
     * @tparam T 元素类型
     */
    template <typename T>
    class Restorer {
    public:
        /**
         * @brief 将一维数组恢复为二维数组
         * @param input 一维输入数组
         * @param shape 目标形状，格式为[rows, cols]
         * @return 恢复后的二维数组
         */
        static std::vector<std::vector<T>> Restore2D(const std::vector<T>& input, const std::vector<int>& shape) {
            if (shape.size() != 2) {
                SLOG_ERROR << "Restorer::Restore2D - Shape must be 2-dimensional";
                return {};
            }

            int rows = shape[0];
            int cols = shape[1];

            if (rows <= 0 || cols <= 0) {
                SLOG_ERROR << "Restorer::Restore2D - Shape dimensions must be positive";
                return {};
            }

            size_t expectedSize = static_cast<size_t>(rows) * static_cast<size_t>(cols);
            if (input.size() != expectedSize) {
                SLOG_ERROR << "Restorer::Restore2D - Input size does not match shape dimensions"
                           << " (expected: " << expectedSize << ", actual: " << input.size() << ")";
                return {};
            }

            std::vector<std::vector<T>> output(rows, std::vector<T>(cols));

            const T* inputPtr = input.data();
            for (int i = 0; i < rows; ++i) {
                T* rowPtr = output[i].data();
                for (int j = 0; j < cols; ++j) {
                    rowPtr[j] = inputPtr[i * cols + j];
                }
            }

            return output;
        }

        /**
         * @brief 将一维数组恢复为三维数组
         * @param input 一维输入数组
         * @param shape 目标形状，格式为[depth, rows, cols]
         * @return 恢复后的三维数组
         */
        static std::vector<std::vector<std::vector<T>>> Restore3D(const std::vector<T>& input,
                                                                  const std::vector<int>& shape) {
            if (shape.size() != 3) {
                SLOG_ERROR << "Restorer::Restore3D - Shape must be 3-dimensional";
                return {};
            }

            int depth = shape[0];
            int rows = shape[1];
            int cols = shape[2];

            if (depth <= 0 || rows <= 0 || cols <= 0) {
                SLOG_ERROR << "Restorer::Restore3D - Shape dimensions must be positive";
                return {};
            }

            size_t expectedSize = static_cast<size_t>(depth) * static_cast<size_t>(rows) * static_cast<size_t>(cols);
            if (input.size() != expectedSize) {
                SLOG_ERROR << "Restorer::Restore3D - Input size does not match shape dimensions"
                           << " (expected: " << expectedSize << ", actual: " << input.size() << ")";
                return {};
            }

            std::vector<std::vector<std::vector<T>>> output(depth,
                                                            std::vector<std::vector<T>>(rows, std::vector<T>(cols)));

            const T* inputPtr = input.data();
            for (int i = 0; i < depth; ++i) {
                for (int j = 0; j < rows; ++j) {
                    T* rowPtr = output[i][j].data();
                    for (int k = 0; k < cols; ++k) {
                        rowPtr[k] = inputPtr[(i * rows + j) * cols + k];
                    }
                }
            }

            return output;
        }
    };

    /**
     * @class ModelWorker
     * @brief 模型工作者基类，采用编译时多态设计模式
     *
     * 本类为模型推理的基类，提供：
     * - TPU设备管理与模型加载
     * - 计算图管理与输入输出节点查询
     * - 线程安全的推理执行
     * - Tensor对象池复用机制
     *
     * 子类不可直接实例化，必须通过继承实现
     */
    class ModelWorker {
    public:
        /**
         * @brief 构造函数
         * @param modelPath 模型文件路径
         * @param tpuId TPU设备ID，默认为0
         * @param ioMode IO模式，默认为SYSIO
         */
        ModelWorker(const std::string& modelPath, int tpuId = 0, IOMode ioMode = IOMode::SYSIO);

        /**
         * @brief 析构函数
         */
        virtual ~ModelWorker();

        /**
         * @brief 检查模型是否准备就绪
         * @return 准备就绪返回true，否则返回false
         */
        bool IsReady() const;

    protected:
        /**
         * @brief 处理输入张量，执行模型推理（使用默认图）
         * @param input 输入数据
         * @param output 输出数据
         * @return 处理结果码
         */
        int Process(const ModelInput& input, ModelOutput& output);

        /**
         * @brief 处理输入张量，执行模型推理（指定计算图）
         * @param graphName 计算图名称
         * @param input 输入数据
         * @param output 输出数据
         * @return 处理结果码
         */
        int Process(const std::string& graphName, const ModelInput& input, ModelOutput& output);

        /**
         * @brief 获取所有计算图名称
         * @return 计算图名称列表
         */
        std::vector<std::string> GetGraphNames() const;

        /**
         * @brief 获取指定计算图的输入节点名称
         * @param graphName 计算图名称
         * @return 输入节点名称列表
         */
        std::vector<std::string> GetInputNames(const std::string& graphName) const;

        /**
         * @brief 获取指定计算图的输出节点名称
         * @param graphName 计算图名称
         * @return 输出节点名称列表
         */
        std::vector<std::string> GetOutputNames(const std::string& graphName) const;

        /**
         * @brief 获取TensorPool统计信息
         * @return TensorPool统计信息字符串
         */
        std::string GetTensorPoolStats() const;

        /**
         * @brief 获取指定计算图的网络信息
         * @param graphName 计算图名称
         * @return bm_net_info_t指针，不存在时返回nullptr
         */
        const bm_net_info_t* GetNetInfo(const std::string& graphName) const;

    private:
        /**
         * @brief 初始化模型
         * @return 初始化成功返回true，否则返回false
         */
        bool Init();

    protected:
        bool mInitialized = false;
        mutable std::mutex mInitLock;
        mutable std::mutex mProcessLock;
        std::string mModelPath;
        int mTpuId;
        IOMode mIoMode;
        bm_handle_t mHandle = nullptr;
        void* mpBmrt = nullptr;
        std::map<std::string, const bm_net_info_t*> mNetInfos;
        std::string mDefaultGraphName;
        std::vector<std::string> mGraphNames;
        std::map<std::string, std::vector<std::string>> mGraphInputNames;
        std::map<std::string, std::vector<std::string>> mGraphOutputNames;
        std::unique_ptr<TensorPool> mTensorPool;
    };

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_WORKER_H
