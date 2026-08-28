/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef NUMCPP_NO_USE_BOOST
#define NUMCPP_NO_USE_BOOST
#endif

#ifndef QIFENG_FRAMEWORK_INCLUDE_MODELS_HM_WORKER_WORKER_H
#define QIFENG_FRAMEWORK_INCLUDE_MODELS_HM_WORKER_WORKER_H

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "common/logger.h"
#include "models_hm/worker/define.h"
#include "models_hm/worker/error.h"

#include "tcim/tcim_runtime.h"

namespace qifeng {

    using FloatMatrix = std::vector<std::vector<float>>;

    /**
     * @enum IOMode
     * @brief IO模式枚举（TCIM版本保留接口兼容，实际均通过Module托管）
     */
    enum class IOMode {
        SYSIO,   ///< 系统内存IO模式
        TPUOSIO  ///< TPU IO模式（保留）
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
     */
    template <typename T>
    class Flattener {
    public:
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
     */
    template <typename T>
    class Restorer {
    public:
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
     * @brief 模型工作者基类（Houmo TCIM 版本）
     *
     * 基于 tcim::Module 实现，提供：
     * - TCIM 模型加载（.hmm/.hmms）
     * - 输入输出节点查询
     * - 线程安全的推理执行（SetInput -> Run -> Sync -> GetOutput）
     *
     * 与 sophon 版本相比，TCIM Module 内部托管输入输出张量内存，
     * 不再需要 TensorPool 池化机制。
     */
    class ModelWorker {
    public:
        ModelWorker(const std::string& modelPath, int tpuId = 0, IOMode ioMode = IOMode::SYSIO);
        virtual ~ModelWorker();
        bool IsReady() const;

    protected:
        int Process(const ModelInput& input, ModelOutput& output);
        int Process(const std::string& graphName, const ModelInput& input, ModelOutput& output);
        std::vector<std::string> GetGraphNames() const;
        std::vector<std::string> GetInputNames(const std::string& graphName) const;
        std::vector<std::string> GetOutputNames(const std::string& graphName) const;
        std::string GetTensorPoolStats() const;

        /**
         * @brief 获取指定输入张量的 TensorInfo（默认图）
         * @param name 输入张量名称
         * @return TensorInfo（TCIM 版本替代 sophon 的 bm_net_info_t）
         */
        tcim::TensorInfo GetInputInfo(const std::string& name) const;

        /**
         * @brief 获取指定输出张量的 TensorInfo（默认图）
         */
        tcim::TensorInfo GetOutputInfo(const std::string& name) const;

        /**
         * @brief 获取指定图的输入张量 TensorInfo
         * @param graphName 图名
         * @param name 输入张量名称
         */
        tcim::TensorInfo GetInputInfo(const std::string& graphName, const std::string& name) const;

        /**
         * @brief 获取指定图的输出张量 TensorInfo
         */
        tcim::TensorInfo GetOutputInfo(const std::string& graphName, const std::string& name) const;

        /**
         * @brief 获取底层 TCIM Module（子类如需直接调用 TCIM API 可使用）
         */
        tcim::Module& GetModule() {
            return *mModule;
        }

        /**
         * @brief 加载额外模型文件并注册为指定图名
         * @param graphName 图名（如 "encoder"、"decoder"）
         * @param filePath 模型文件路径（.hmm）
         * @return true 加载成功
         */
        bool LoadGraph(const std::string& graphName, const std::string& filePath);

    private:
        bool Init();
        ///< 加载单个模型文件，注册输入输出节点名，返回 Module
        std::shared_ptr<tcim::Module> LoadModuleFile(const std::string& filePath, const std::string& graphName);

    protected:
        bool mInitialized = false;
        mutable std::mutex mInitLock;
        mutable std::mutex mProcessLock;
        std::string mModelPath;
        int mTpuId;
        IOMode mIoMode;
        std::shared_ptr<tcim::Module> mModule;  ///< TCIM 默认模型模块（向后兼容）
        tcim::Stream mStream;                   ///< TCIM 推理流
        std::string mDefaultGraphName;          ///< 默认图名（TCIM 单模型，取模型文件名）
        std::vector<std::string> mGraphNames;   ///< 图名列表
        std::map<std::string, std::vector<std::string>> mGraphInputNames;
        std::map<std::string, std::vector<std::string>> mGraphOutputNames;
        std::map<std::string, std::shared_ptr<tcim::Module>> mModules;  ///< 图名 -> Module 映射（多模型支持）
    };

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_INCLUDE_MODELS_HM_WORKER_WORKER_H
