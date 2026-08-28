/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/logger.h"
#include "models/worker/tensor_pool.h"

namespace qifeng {

    /* ==================== BmTensor 实现 ==================== */

    BmTensor::BmTensor() {
        tensor.dtype = BM_FLOAT32;
        tensor.shape.num_dims = 0;
        tensor.st_mode = BM_STORE_1N;
    }

    BmTensor::BmTensor(bm_handle_t handle, void* pBmrt, bm_data_type_t dtype, bm_shape_t shape)
        : mHandle(handle), mpBmrt(pBmrt), mOwnsMem(false) {
        tensor.dtype = dtype;
        tensor.st_mode = BM_STORE_1N;

        bool ok = bmrt_tensor(&tensor, pBmrt, dtype, shape);
        if (ok) {
            mOwnsMem = true;
        } else {
            SLOG_ERROR << "BmTensor: bmrt_tensor failed to allocate device memory";
        }
    }

    BmTensor::~BmTensor() { freeMem(); }

    BmTensor::BmTensor(BmTensor&& other) noexcept {
        tensor = other.tensor;
        mHandle = other.mHandle;
        mpBmrt = other.mpBmrt;
        mOwnsMem = other.mOwnsMem;

        other.mOwnsMem = false;
        other.mHandle = nullptr;
        other.mpBmrt = nullptr;
        other.tensor = bm_tensor_t{};
    }

    BmTensor& BmTensor::operator=(BmTensor&& other) noexcept {
        if (this != &other) {
            freeMem();

            tensor = other.tensor;
            mHandle = other.mHandle;
            mpBmrt = other.mpBmrt;
            mOwnsMem = other.mOwnsMem;

            other.mOwnsMem = false;
            other.mHandle = nullptr;
            other.mpBmrt = nullptr;
            other.tensor = bm_tensor_t{};
        }
        return *this;
    }

    bm_tensor_t* BmTensor::get() { return &tensor; }

    const bm_tensor_t* BmTensor::get() const { return &tensor; }

    bm_shape_t BmTensor::shape() const { return tensor.shape; }

    void BmTensor::reshape(bm_handle_t handle, void* pBmrt, bm_shape_t newShape) {
        size_t newByteSize = bmrt_data_type_size(tensor.dtype);
        uint64_t elemCount = bmrt_shape_count(&newShape);
        newByteSize *= static_cast<size_t>(elemCount);

        size_t allocSize = deviceMemSize();

        if (newByteSize > allocSize) {
            SLOG_DEBUG << "BmTensor::reshape - reallocating device mem, old size: " << allocSize
                       << ", new needed: " << newByteSize;

            freeMem();

            bool ok = bmrt_tensor(&tensor, pBmrt, tensor.dtype, newShape);
            if (ok) {
                mHandle = handle;
                mpBmrt = pBmrt;
                mOwnsMem = true;
            } else {
                SLOG_ERROR << "BmTensor::reshape - bmrt_tensor reallocation failed";
            }
        } else {
            tensor.shape = newShape;
        }
    }

    size_t BmTensor::size() const { return static_cast<size_t>(bmrt_shape_count(&tensor.shape)); }

    size_t BmTensor::byteSize() const { return bmrt_tensor_bytesize(&tensor); }

    size_t BmTensor::deviceMemSize() const { return static_cast<size_t>(tensor.device_mem.size); }

    bool BmTensor::ownsMem() const { return mOwnsMem; }

    void BmTensor::freeMem() {
        if (mOwnsMem && mpBmrt) {
            bmrt_free_device(mpBmrt, tensor.device_mem);
            mOwnsMem = false;
        }
    }

    /* ==================== TensorPool 实现 ==================== */

    TensorPool::TensorWrapper::TensorWrapper(BmTensor&& t, bm_shape_t shape)
        : tensor(std::move(t)), originalShape(shape) {
    }

    TensorPool::TensorPool(size_t maxPoolSize) : mMaxPoolSize(maxPoolSize) {
        SLOG_DEBUG << "TensorPool initialized with max pool size: " << maxPoolSize;
    }

    TensorPool::~TensorPool() {
        Clear();
        FLOG_DEBUG("TensorPool destroyed");
    }

    std::string TensorPool::GenerateKey(const std::string& graphName, const std::string& tensorName,
                                        bool isInput) const {
        return graphName + ":" + tensorName + ":" + (isInput ? "input" : "output");
    }

    BmTensor TensorPool::Acquire(bm_handle_t handle, void* pBmrt, const std::string& graphName,
                                 const std::string& tensorName, bool isInput, const bm_net_info_t* netInfo) {
        std::string key = GenerateKey(graphName, tensorName, isInput);

        std::lock_guard<std::mutex> lock(mMutex);

        if (!mPool[key].empty()) {
            TensorWrapper wrapper = std::move(mPool[key].back());
            mPool[key].pop_back();
            mActiveTensors[key]++;

            auto shapeIt = mOriginalShapes.find(key);
            if (shapeIt != mOriginalShapes.end() && shapeIt->second.num_dims > 0) {
                wrapper.tensor.reshape(handle, pBmrt, shapeIt->second);
                SLOG_DEBUG << "TensorPool: Reusing tensor from pool, restored shape, key: " << key;
            }

            SLOG_DEBUG << "TensorPool: Reusing tensor, key: " << key << ", pool size: " << mPool[key].size()
                       << ", active: " << mActiveTensors[key];
            return std::move(wrapper.tensor);
        }

        BmTensor tensor;
        if (netInfo) {
            const char* const* names = isInput ? netInfo->input_names : netInfo->output_names;
            int nameCount = isInput ? netInfo->input_num : netInfo->output_num;
            int idx = -1;
            for (int i = 0; i < nameCount; ++i) {
                if (names && names[i] && tensorName == names[i]) {
                    idx = i;
                    break;
                }
            }

            if (idx >= 0 && netInfo->stage_num > 0) {
                bm_data_type_t dtype =
                    isInput ? netInfo->input_dtypes[idx] : netInfo->output_dtypes[idx];
                bm_shape_t shape = isInput ? netInfo->stages[0].input_shapes[idx]
                                           : netInfo->stages[0].output_shapes[idx];

                tensor = BmTensor(handle, pBmrt, dtype, shape);
                mOriginalShapes[key] = shape;

                std::string shapeStr;
                for (int i = 0; i < shape.num_dims; ++i) {
                    shapeStr += std::to_string(shape.dims[i]);
                    if (i < shape.num_dims - 1)
                        shapeStr += "x";
                }
                SLOG_DEBUG << "TensorPool: Created new tensor, shape: [" << shapeStr << "], key: " << key;
            } else {
                SLOG_ERROR << "TensorPool: Tensor name not found in net_info, key: " << key;
            }
        } else {
            SLOG_ERROR << "TensorPool: netInfo is null, key: " << key;
        }

        mActiveTensors[key]++;
        return tensor;
    }

    void TensorPool::Release(const std::string& graphName, const std::string& tensorName, bool isInput,
                             BmTensor tensor) {
        if (!tensor.ownsMem()) {
            SLOG_WARN << "TensorPool: Attempted to release tensor without device memory";
            return;
        }

        std::string key = GenerateKey(graphName, tensorName, isInput);

        std::lock_guard<std::mutex> lock(mMutex);

        if (mActiveTensors[key] > 0) {
            mActiveTensors[key]--;
        }

        if (mPool[key].size() >= mMaxPoolSize) {
            SLOG_DEBUG << "TensorPool: Pool full, discarding tensor, key: " << key;
            return;  // BmTensor 析构会自动释放设备内存
        }

        bm_shape_t currentShape = tensor.shape();
        TensorWrapper wrapper(std::move(tensor), currentShape);
        mPool[key].push_back(std::move(wrapper));
        SLOG_DEBUG << "TensorPool: Released tensor to pool, key: " << key << ", pool size: " << mPool[key].size()
                   << ", active: " << mActiveTensors[key];
    }

    std::unordered_map<std::string, BmTensor> TensorPool::AcquireInputTensors(bm_handle_t handle, void* pBmrt,
                                                                                const std::string& graphName,
                                                                                const bm_net_info_t* netInfo) {
        std::unordered_map<std::string, BmTensor> tensors;

        if (!netInfo) {
            SLOG_ERROR << "TensorPool: netInfo is null for graph: " << graphName;
            return tensors;
        }

        for (int i = 0; i < netInfo->input_num; ++i) {
            if (netInfo->input_names && netInfo->input_names[i]) {
                std::string name = netInfo->input_names[i];
                BmTensor tensor = Acquire(handle, pBmrt, graphName, name, true, netInfo);
                if (tensor.ownsMem()) {
                    tensors[name] = std::move(tensor);
                }
            }
        }

        SLOG_DEBUG << "TensorPool: Acquired " << tensors.size() << " input tensors for graph: " << graphName;
        return tensors;
    }

    std::unordered_map<std::string, BmTensor> TensorPool::AcquireOutputTensors(bm_handle_t handle, void* pBmrt,
                                                                                 const std::string& graphName,
                                                                                 const bm_net_info_t* netInfo) {
        std::unordered_map<std::string, BmTensor> tensors;

        if (!netInfo) {
            SLOG_ERROR << "TensorPool: netInfo is null for graph: " << graphName;
            return tensors;
        }

        for (int i = 0; i < netInfo->output_num; ++i) {
            if (netInfo->output_names && netInfo->output_names[i]) {
                std::string name = netInfo->output_names[i];
                BmTensor tensor = Acquire(handle, pBmrt, graphName, name, false, netInfo);
                if (tensor.ownsMem()) {
                    tensors[name] = std::move(tensor);
                }
            }
        }

        SLOG_DEBUG << "TensorPool: Acquired " << tensors.size() << " output tensors for graph: " << graphName;
        return tensors;
    }

    void TensorPool::ReleaseTensors(const std::string& graphName,
                                    std::unordered_map<std::string, BmTensor>& tensors, bool isInput) {
        for (auto& [name, tensor] : tensors) {
            Release(graphName, name, isInput, std::move(tensor));
        }

        tensors.clear();
        std::string tensorType = isInput ? "input" : "output";
        SLOG_DEBUG << "TensorPool: Released all " << tensorType << " tensors for graph: " << graphName;
    }

    void TensorPool::Clear() {
        std::lock_guard<std::mutex> lock(mMutex);

        size_t totalTensors = 0;
        for (auto& [key, pool] : mPool) {
            totalTensors += pool.size();
            pool.clear();
        }

        mPool.clear();
        mActiveTensors.clear();
        mOriginalShapes.clear();

        SLOG_INFO << "TensorPool: Cleared all pools, released " << totalTensors << " tensors";
    }

    std::string TensorPool::GetStats() const {
        std::lock_guard<std::mutex> lock(mMutex);

        std::string stats = "TensorPool Stats:\n";
        stats += "  Total pool keys: " + std::to_string(mPool.size()) + "\n";

        size_t totalInPool = 0;
        size_t totalActive = 0;

        for (const auto& [key, pool] : mPool) {
            size_t inPool = pool.size();
            size_t active = mActiveTensors.count(key) ? mActiveTensors.at(key) : 0;

            totalInPool += inPool;
            totalActive += active;

            stats +=
                "  Key: " + key + ", In pool: " + std::to_string(inPool) + ", Active: " + std::to_string(active) + "\n";
        }

        stats += "  Total in pool: " + std::to_string(totalInPool) + "\n";
        stats += "  Total active: " + std::to_string(totalActive) + "\n";
        stats += "  Max pool size: " + std::to_string(mMaxPoolSize) + "\n";

        return stats;
    }

    void TensorPool::SetMaxPoolSize(size_t maxPoolSize) {
        std::lock_guard<std::mutex> lock(mMutex);
        mMaxPoolSize = maxPoolSize;
        SLOG_DEBUG << "TensorPool: Max pool size set to " << maxPoolSize;
    }

}  // namespace qifeng
