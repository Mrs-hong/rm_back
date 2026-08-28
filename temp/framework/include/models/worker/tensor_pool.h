/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_TENSORPOOL_H
#define QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_TENSORPOOL_H

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "bmdef.h"
#include "bmlib_runtime.h"
#include "bmruntime_interface.h"
#include "bmruntime_legacy.h"

namespace qifeng {

    /**
     * @class BmTensor
     * @brief bm_tensor_t 的 RAII 包装类，自动管理设备内存生命周期
     *
     * 该类封装了 bmruntime 的 bm_tensor_t 结构体，通过构造时分配设备内存、
     * 析构时释放设备内存的方式实现自动内存管理。仅支持移动语义。
     */
    class BmTensor {
    public:
        bm_tensor_t tensor{};

        BmTensor();

        /**
         * @brief 构造并分配设备内存
         * @param handle BM设备句柄
         * @param pBmrt bmruntime指针
         * @param dtype 数据类型
         * @param shape 张量形状
         */
        BmTensor(bm_handle_t handle, void* pBmrt, bm_data_type_t dtype, bm_shape_t shape);

        ~BmTensor();

        BmTensor(const BmTensor&) = delete;
        BmTensor& operator=(const BmTensor&) = delete;
        BmTensor(BmTensor&& other) noexcept;
        BmTensor& operator=(BmTensor&& other) noexcept;

        bm_tensor_t* get();
        const bm_tensor_t* get() const;

        bm_shape_t shape() const;

        /**
         * @brief 重新设置张量形状
         * @note 若新形状的字节数超过已分配设备内存，会重新分配
         * @param handle BM设备句柄
         * @param pBmrt bmruntime指针
         * @param newShape 新形状
         */
        void reshape(bm_handle_t handle, void* pBmrt, bm_shape_t newShape);

        /** @brief 当前形状的元素数量 */
        size_t size() const;

        /** @brief 当前形状的字节数（元素数 × dtype大小） */
        size_t byteSize() const;

        /** @brief 已分配设备内存的字节数 */
        size_t deviceMemSize() const;

        /** @brief 是否持有设备内存 */
        bool ownsMem() const;

    private:
        bm_handle_t mHandle = nullptr;
        void* mpBmrt = nullptr;
        bool mOwnsMem = false;

        void freeMem();
    };

    /**
     * @class TensorPool
     * @brief BmTensor 对象池，复用设备内存分配以减少开销
     *
     * 通过池化 BmTensor 对象（含已分配的设备内存），避免每次推理都进行
     * bm_malloc_device_mem / bm_free_device_mem 操作。
     */
    class TensorPool {
    public:
        explicit TensorPool(size_t maxPoolSize = 1);

        ~TensorPool();

        BmTensor Acquire(bm_handle_t handle, void* pBmrt, const std::string& graphName,
                         const std::string& tensorName, bool isInput, const bm_net_info_t* netInfo);

        void Release(const std::string& graphName, const std::string& tensorName, bool isInput, BmTensor tensor);

        std::unordered_map<std::string, BmTensor> AcquireInputTensors(bm_handle_t handle, void* pBmrt,
                                                                       const std::string& graphName,
                                                                       const bm_net_info_t* netInfo);

        std::unordered_map<std::string, BmTensor> AcquireOutputTensors(bm_handle_t handle, void* pBmrt,
                                                                        const std::string& graphName,
                                                                        const bm_net_info_t* netInfo);

        void ReleaseTensors(const std::string& graphName, std::unordered_map<std::string, BmTensor>& tensors,
                            bool isInput);

        void Clear();

        std::string GetStats() const;

        void SetMaxPoolSize(size_t maxPoolSize);

    private:
        std::string GenerateKey(const std::string& graphName, const std::string& tensorName, bool isInput) const;

        struct TensorWrapper {
            BmTensor tensor;
            bm_shape_t originalShape;

            TensorWrapper() = default;
            TensorWrapper(BmTensor&& t, bm_shape_t shape);
        };

        std::unordered_map<std::string, std::deque<TensorWrapper>> mPool;
        std::unordered_map<std::string, bm_shape_t> mOriginalShapes;
        std::unordered_map<std::string, size_t> mActiveTensors;
        size_t mMaxPoolSize;
        mutable std::mutex mMutex;
    };

}  // namespace qifeng

#endif  // QIFENG_FRAMEWORK_INCLUDE_MODELS_WORKER_TENSORPOOL_H
