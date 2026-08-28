/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "common/utils/batch_executor.h"

BatchExecutor& BatchExecutor::GetInstance(size_t numThreads) {
    static BatchExecutor Instance(numThreads);
    return Instance;
}

BatchExecutor::BatchExecutor(size_t numThreads) : mStop(false) {
    if (numThreads == 0) {
        numThreads = 1;
    }
    for (size_t i = 0; i < numThreads; ++i) {
        mWorkers.emplace_back([this] { WorkerLoop(); });
    }
}

void BatchExecutor::WorkerLoop() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(this->mQueueMutex);
            this->mCondition.wait(lock, [this] { return this->mStop || !this->mTasks.empty(); });
            if (this->mStop && this->mTasks.empty()) {
                return;
            }
            task = std::move(this->mTasks.front());
            this->mTasks.pop();
        }
        task();
    }
}

BatchExecutor::~BatchExecutor() {
    Shutdown();
}

size_t BatchExecutor::GetPendingTaskCount() const {
    std::unique_lock<std::mutex> lock(mQueueMutex);
    return mTasks.size();
}

size_t BatchExecutor::GetThreadCount() const {
    return mWorkers.size();
}

void BatchExecutor::Shutdown() {
    {
        std::unique_lock<std::mutex> lock(mQueueMutex);
        mStop = true;
    }

    mCondition.notify_all();
    for (std::thread& worker : mWorkers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    mWorkers.clear();
}

void BatchExecutor::Restart(size_t numThreads) {
    Shutdown();
    ClearTasks();
    CreateThreads(numThreads);
}

void BatchExecutor::ClearTasks() {
    std::unique_lock<std::mutex> lock(mQueueMutex);
    mStop = false;
    while (!mTasks.empty()) {
        mTasks.pop();
    }
}

void BatchExecutor::CreateThreads(size_t numThreads) {
    if (numThreads == 0) {
        numThreads = 1;
    }
    for (size_t i = 0; i < numThreads; ++i) {
        mWorkers.emplace_back([this] { WorkerLoop(); });
    }
}