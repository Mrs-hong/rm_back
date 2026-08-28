/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_COMMON_RINGBUFFER_H
#define QIFENG_FRAMEWORK_COMMON_RINGBUFFER_H

#include <array>
#include <cstddef>
#include <stdexcept>
#include <vector>

template <typename T, size_t Capacity>
class RingBuffer {
public:
    RingBuffer() : mHead(0), mTail(0), mCount(0) {
    }

    // ---------- 接口1：添加一个块 ----------
    void Push(const T& value) {
        mBuffer[mTail] = value;
        mTail = (mTail + 1) % Capacity;
        if (mCount == Capacity) {
            mHead = (mHead + 1) % Capacity;  // 满了则覆盖最旧的
        } else {
            mCount++;
        }
    }

    // 移动版本（避免拷贝，适合大块数据）
    void Push(T&& value) {
        mBuffer[mTail] = std::move(value);
        mTail = (mTail + 1) % Capacity;
        if (mCount == Capacity) {
            mHead = (mHead + 1) % Capacity;
        } else {
            mCount++;
        }
    }

    // ---------- 接口2：获取所有块的数据（按时间顺序，从旧到新） ----------
    std::vector<T> GetAll() const {
        std::vector<T> result;
        result.reserve(mCount);
        for (size_t i = 0; i < mCount; ++i) {
            result.push_back(mBuffer[(mHead + i) % Capacity]);
        }
        return result;
    }

    // 如果需要避免拷贝，返回引用（但调用方需注意生命周期）
    const std::array<T, Capacity>& GetBuffer() const {
        return mBuffer;
    }
    size_t GetHead() const {
        return mHead;
    }
    size_t GetCount() const {
        return mCount;
    }

    // ---------- 获取最后一个块 ----------
    // 缓冲区为空时行为未定义，调用前请先检查 !empty()
    const T& GetBackBlock() const {
        return mBuffer[(mTail + Capacity - 1) % Capacity];
    }
    T& GetBackBlock() {
        return mBuffer[(mTail + Capacity - 1) % Capacity];
    }

    // ---------- 辅助接口 ----------
    size_t size() const {
        return mCount;
    }
    bool empty() const {
        return mCount == 0;
    }
    bool full() const {
        return mCount == Capacity;
    }
    size_t capacity() const {
        return Capacity;
    }
    void clear() {
        mHead = mTail = mCount = 0;
    }

private:
    std::array<T, Capacity> mBuffer;
    size_t mHead;
    size_t mTail;
    size_t mCount;
};

#endif  // QIFENG_FRAMEWORK_COMMON_RINGBUFFER_H
