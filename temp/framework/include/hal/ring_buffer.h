/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef HAL_RING_BUFFER_H
#define HAL_RING_BUFFER_H

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace qifeng {

    template <typename T>
    class RingBuffer {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

    public:
        explicit RingBuffer(uint32_t capacity)
            : mCapacity(RoundUpToPowerOf2(capacity)), mBuffer(mCapacity), mWritePos(0), mReadPos(0) {
        }

        ~RingBuffer() = default;

        RingBuffer(const RingBuffer&) = delete;
        RingBuffer& operator=(const RingBuffer&) = delete;
        RingBuffer(RingBuffer&&) = delete;
        RingBuffer& operator=(RingBuffer&&) = delete;

        size_t Write(const T* data, size_t count) {
            size_t available = WriteAvailable();
            size_t toWrite = std::min(count, available);
            if (toWrite == 0) {
                return 0;
            }

            const uint32_t wp = mWritePos.load(std::memory_order_relaxed);  // NOLINT(cppcoreguidelines-init-variables)
            uint32_t mask = mCapacity - 1;
            uint32_t idx = wp & mask;

            uint32_t firstChunk = std::min(static_cast<uint32_t>(toWrite), mCapacity - idx);
            std::memcpy(mBuffer.data() + idx, data, firstChunk * sizeof(T));

            if (toWrite > firstChunk) {
                std::memcpy(mBuffer.data(), data + firstChunk, (toWrite - firstChunk) * sizeof(T));
            }

            // 位置计数器不掩码, 保持单调递增, uint32自然回绕保证减法正确
            mWritePos.store(wp + static_cast<uint32_t>(toWrite), std::memory_order_release);
            return toWrite;
        }

        size_t Read(T* data, size_t count) {
            size_t available = ReadAvailable();
            size_t toRead = std::min(count, available);
            if (toRead == 0) {
                return 0;
            }

            const uint32_t rp = mReadPos.load(std::memory_order_relaxed);  // NOLINT(cppcoreguidelines-init-variables)
            uint32_t mask = mCapacity - 1;
            uint32_t idx = rp & mask;

            uint32_t firstChunk = std::min(static_cast<uint32_t>(toRead), mCapacity - idx);
            std::memcpy(data, mBuffer.data() + idx, firstChunk * sizeof(T));

            if (toRead > firstChunk) {
                std::memcpy(data + firstChunk, mBuffer.data(), (toRead - firstChunk) * sizeof(T));
            }

            mReadPos.store(rp + static_cast<uint32_t>(toRead), std::memory_order_release);
            return toRead;
        }

        size_t WriteAvailable() const {
            const uint32_t wp = mWritePos.load(std::memory_order_relaxed);  // NOLINT(cppcoreguidelines-init-variables)
            const uint32_t rp = mReadPos.load(std::memory_order_acquire);   // NOLINT(cppcoreguidelines-init-variables)
            return static_cast<size_t>(mCapacity - (wp - rp));
        }

        size_t ReadAvailable() const {
            const uint32_t wp = mWritePos.load(std::memory_order_acquire);  // NOLINT(cppcoreguidelines-init-variables)
            const uint32_t rp = mReadPos.load(std::memory_order_relaxed);   // NOLINT(cppcoreguidelines-init-variables)
            return static_cast<size_t>(wp - rp);  // NOLINT(cppcoreguidelines-narrowing-conversions)
        }

        // 调用方须确保无并发Write/Read, 通常在设备重置时使用
        void Reset() {
            mWritePos.store(0, std::memory_order_relaxed);
            mReadPos.store(0, std::memory_order_relaxed);
        }

        uint32_t Capacity() const {
            return mCapacity;
        }

    private:
        static uint32_t RoundUpToPowerOf2(uint32_t v) {
            if (v == 0) {
                return 1;
            }
            if (v > (1U << 30)) {
                return 1U << 30;
            }
            v--;
            v |= v >> 1;
            v |= v >> 2;
            v |= v >> 4;
            v |= v >> 8;
            v |= v >> 16;
            v++;
            return v;
        }

        uint32_t mCapacity {};
        std::vector<T> mBuffer {};
        std::atomic<uint32_t> mWritePos {};
        std::atomic<uint32_t> mReadPos {};
    };

}  // namespace qifeng

#endif  // HAL_RING_BUFFER_H
