/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_BOUNDED_QUEUE_H
#define HAL_BOUNDED_QUEUE_H

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <vector>

namespace qifeng {

    template <typename T>
    class BoundedQueue {
    public:
        explicit BoundedQueue(size_t capacity)
            : mCapacity(capacity), mBuffer(capacity), mHead(0), mTail(0), mSize(0), mStop(false) {
        }

        ~BoundedQueue() {
            Stop();
        }

        BoundedQueue(const BoundedQueue&) = delete;
        BoundedQueue& operator=(const BoundedQueue&) = delete;

        BoundedQueue(BoundedQueue&&) = delete;
        BoundedQueue& operator=(BoundedQueue&&) = delete;

    public:
        /**
         * @brief 拷贝入队
         */
        bool Push(const T& item) {
            std::lock_guard<std::mutex> lock(mMutex);

            if (mStop || IsFullLocked()) {
                return false;
            }

            mBuffer[mTail] = item;

            AdvanceTailLocked();

            mCond.notify_one();
            return true;
        }

        /**
         * @brief 移动入队
         */
        bool Push(T&& item) {
            std::lock_guard<std::mutex> lock(mMutex);

            if (mStop || IsFullLocked()) {
                return false;
            }

            mBuffer[mTail] = std::move(item);

            AdvanceTailLocked();

            mCond.notify_one();
            return true;
        }

        /**
         * @brief 原地构造
         */
        template <typename... Args>
        bool Emplace(Args&&... args) {
            std::lock_guard<std::mutex> lock(mMutex);

            if (mStop || IsFullLocked()) {
                return false;
            }

            mBuffer[mTail].emplace(std::forward<Args>(args)...);

            AdvanceTailLocked();

            mCond.notify_one();
            return true;
        }

        /**
         * @brief 非阻塞出队
         */
        bool TryPop(T& item) {
            std::lock_guard<std::mutex> lock(mMutex);

            if (IsEmptyLocked()) {
                return false;
            }

            PopLocked(item);
            return true;
        }

        /**
         * @brief 阻塞等待出队
         */
        bool WaitPop(T& item) {
            std::unique_lock<std::mutex> lock(mMutex);

            mCond.wait(lock, [this]() { return mStop || !IsEmptyLocked(); });

            if (mStop && IsEmptyLocked()) {
                return false;
            }

            PopLocked(item);
            return true;
        }

        /**
         * @brief 超时等待出队
         */
        template <typename Rep, typename Period>
        bool WaitPop(T& item, const std::chrono::duration<Rep, Period>& timeout) {
            std::unique_lock<std::mutex> lock(mMutex);

            bool ret = mCond.wait_for(lock, timeout, [this]() { return mStop || !IsEmptyLocked(); });

            if (!ret) {
                return false;
            }

            if (mStop && IsEmptyLocked()) {
                return false;
            }

            PopLocked(item);
            return true;
        }

        /**
         * @brief 停止队列
         */
        void Stop() {
            {
                std::lock_guard<std::mutex> lock(mMutex);

                if (mStop) {
                    return;
                }

                mStop = true;
            }

            mCond.notify_all();
        }

        /**
         * @brief 清空队列
         */
        void Clear() {
            std::lock_guard<std::mutex> lock(mMutex);

            for (auto& item : mBuffer) {
                item.reset();
            }

            mHead = 0;
            mTail = 0;
            mSize = 0;
        }

        /**
         * @brief 队列是否为空
         */
        bool Empty() const {
            std::lock_guard<std::mutex> lock(mMutex);
            return IsEmptyLocked();
        }

        /**
         * @brief 队列是否已满
         */
        bool Full() const {
            std::lock_guard<std::mutex> lock(mMutex);
            return IsFullLocked();
        }

        /**
         * @brief 当前元素数量
         */
        size_t Size() const {
            std::lock_guard<std::mutex> lock(mMutex);
            return mSize;
        }

        /**
         * @brief 队列容量
         */
        size_t Capacity() const {
            return mCapacity;
        }

        /**
         * @brief 是否已经停止
         */
        bool IsStopped() const {
            std::lock_guard<std::mutex> lock(mMutex);
            return mStop;
        }

    private:
        bool IsEmptyLocked() const {
            return mSize == 0;
        }

        bool IsFullLocked() const {
            return mSize >= mCapacity;
        }

        void AdvanceTailLocked() {
            mTail = (mTail + 1) % mCapacity;
            ++mSize;
        }

        void AdvanceHeadLocked() {
            mHead = (mHead + 1) % mCapacity;
            --mSize;
        }

        void PopLocked(T& item) {
            item = std::move(*(mBuffer[mHead]));

            mBuffer[mHead].reset();

            AdvanceHeadLocked();
        }

    private:
        const size_t mCapacity = 0;

        std::vector<std::optional<T>> mBuffer {};

        size_t mHead = 0;
        size_t mTail = 0;
        size_t mSize = 0;

        bool mStop = false;

        mutable std::mutex mMutex;

        std::condition_variable mCond;
    };

}  // namespace qifeng

#endif  // HAL_BOUNDED_QUEUE_H
