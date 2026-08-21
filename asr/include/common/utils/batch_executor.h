/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#ifndef QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_BATCH_EXECUTOR_H
#define QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_BATCH_EXECUTOR_H

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

/**
 * @brief 批量执行器，用于并发执行一组函数
 *
 * 支持线程池管理、任务队列，提供同步和异步执行方式
 * 采用单例模式设计
 */
class BatchExecutor {
public:
    /**
     * @brief 获取单例实例
     * @param numThreads 线程池中的线程数量，默认为硬件并发数
     * @return BatchExecutor& 单例引用
     */
    static BatchExecutor& GetInstance(size_t numThreads = std::thread::hardware_concurrency());

    /**
     * @brief 向线程池中加入单个异步任务并等待结果
     * @tparam F 函数类型
     * @tparam Args 参数类型
     * @param f 要执行的函数
     * @param args 函数参数
     * @return 函数执行结果
     */
    template <class F, class... Args>
    auto ExecSingleTask(F&& f, Args&&... args) -> typename std::result_of<F(Args...)>::type;

    /**
     * @brief 向线程池中加入一个异步任务，返回future用于等待结果
     * @tparam F 函数类型
     * @tparam Args 参数类型
     * @param f 要执行的函数
     * @param args 函数参数
     * @return std::future<T> 用于获取异步结果
     */
    template <class F, class... Args>
    auto AddTask(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>;

    /**
     * @brief 获取当前待处理的任务数量
     * @return 待处理任务数量
     */
    size_t GetPendingTaskCount() const;

    /**
     * @brief 获取线程池中的线程数量
     * @return 线程数量
     */
    size_t GetThreadCount() const;

    /**
     * @brief 停止线程池，等待所有任务完成
     */
    void Shutdown();

    /**
     * @brief 重新启动线程池
     * @param numThreads 新的线程数量
     */
    void Restart(size_t numThreads = std::thread::hardware_concurrency());

    // 禁用拷贝构造、赋值和移动操作
    BatchExecutor(const BatchExecutor&) = delete;
    BatchExecutor& operator=(const BatchExecutor&) = delete;
    BatchExecutor(BatchExecutor&&) = delete;
    BatchExecutor& operator=(BatchExecutor&&) = delete;

private:
    // 辅助方法：工作线程循环函数
    void WorkerLoop();
    // 辅助方法：清空任务队列
    void ClearTasks();
    // 辅助方法：创建线程池线程
    void CreateThreads(size_t numThreads);
    /**
     * @brief 私有构造函数
     * @param numThreads 线程池中的线程数量
     */
    explicit BatchExecutor(size_t numThreads);

    /**
     * @brief 析构函数，等待所有任务完成
     */
    ~BatchExecutor();

    // 工作线程
    std::vector<std::thread> mWorkers;

    // 任务队列
    std::queue<std::function<void()>> mTasks;

    // 同步原语
    mutable std::mutex mQueueMutex;
    std::condition_variable mCondition;

    // 停止标志
    bool mStop;
};

// 模板方法的实现必须在头文件中
template <class F, class... Args>
auto BatchExecutor::ExecSingleTask(F&& f, Args&&... args) -> typename std::result_of<F(Args...)>::type {
    using return_type = typename std::result_of<F(Args...)>::type;

    auto task =
        std::make_shared<std::packaged_task<return_type()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<return_type> result = task->get_future();

    {
        std::unique_lock<std::mutex> lock(mQueueMutex);

        if (mStop) {
            throw std::runtime_error("BatchExecutor has been stopped");
        }

        mTasks.emplace([task]() { (*task)(); });
    }

    mCondition.notify_one();
    return result.get();
}

template <class F, class... Args>
auto BatchExecutor::AddTask(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> {
    using return_type = typename std::result_of<F(Args...)>::type;

    auto task =
        std::make_shared<std::packaged_task<return_type()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<return_type> result = task->get_future();

    {
        std::unique_lock<std::mutex> lock(mQueueMutex);

        if (mStop) {
            throw std::runtime_error("BatchExecutor has been stopped");
        }

        mTasks.emplace([task]() { (*task)(); });
    }

    mCondition.notify_one();
    return result;
}

#endif  // QIFENG_FRAMEWORK_INCLUDE_COMMON_UTILS_BATCH_EXECUTOR_H