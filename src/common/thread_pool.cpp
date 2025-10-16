#include "common/thread_pool.h"
#include <algorithm>

namespace memchainer {

// ============================================================================
// 全局线程池实例
// ============================================================================

std::shared_ptr<ThreadPool> globalThreadPool;

// ============================================================================
// ThreadPool 实现
// ============================================================================

ThreadPool::ThreadPool(size_t numThreads) {
    // 确保至少有一个线程
    numThreads = std::max<size_t>(1, numThreads);
    
    // 预留空间以避免重新分配
    workers.reserve(numThreads);
    
    // 创建工作线程
    try {
        for (size_t i = 0; i < numThreads; ++i) {
            workers.emplace_back(&ThreadPool::workerThread, this);
        }
    } catch (...) {
        // 如果创建线程失败，清理已创建的线程
        stop.store(true, std::memory_order_release);
        queueCondition.notify_all();
        
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        throw;
    }
}

ThreadPool::~ThreadPool() {
    // 设置停止标志
    stop.store(true, std::memory_order_release);
    
    // 唤醒所有等待的线程
    queueCondition.notify_all();
    
    // 等待所有线程完成
    for (auto& worker : workers) {
        if (worker.joinable()) {
            try {
                worker.join();
            } catch (const std::exception&) {
                // 忽略 join 异常
            }
        }
    }
    
    // 清理队列中的任务
    std::queue<std::function<void()>> empty;
    std::swap(tasks, empty);
}

void ThreadPool::wait() {
    std::unique_lock<std::mutex> lock(completionMutex);
    
    // 等待所有任务完成（队列中的和正在执行的）
    completionCondition.wait(lock, [this] {
        return pendingTaskCount.load(std::memory_order_relaxed) == 0 && 
               activeTaskCount.load(std::memory_order_relaxed) == 0;
    });
}

void ThreadPool::workerThread() {
    while (true) {
        std::function<void()> task;
        
        // 获取任务
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            
            // 等待任务或停止信号
            queueCondition.wait(lock, [this] {
                return stop.load(std::memory_order_relaxed) || !tasks.empty();
            });
            
            // 如果线程池停止且没有待处理的任务，退出
            if (stop.load(std::memory_order_relaxed) && tasks.empty()) {
                return;
            }
            
            // 从队列中取出任务
            if (!tasks.empty()) {
                task = std::move(tasks.front());
                tasks.pop();
            }
        }
        
        // 执行任务（在锁外执行以提高并发性）
        if (task) {
            try {
                task();
            } catch (const std::exception&) {
                // 捕获异常以防止线程崩溃
                // 具体的异常处理由 packaged_task 和 future 机制处理
            } catch (...) {
                // 捕获所有其他异常
            }
        }
    }
}

// ============================================================================
// 全局线程池初始化
// ============================================================================

/**
 * @brief 全局线程池初始化器
 * 
 * 使用 RAII 模式确保线程池在程序启动时自动初始化
 */
class ThreadPoolInitializer {
public:
    ThreadPoolInitializer() {
        try {
            // 创建全局线程池，使用默认线程数（CPU 核心数）
            globalThreadPool = std::make_shared<ThreadPool>();
        } catch (const std::exception&) {
            // 如果初始化失败，保持 nullptr
            // 用户代码应该检查 globalThreadPool 是否为空
        }
    }
    
    ~ThreadPoolInitializer() {
        // 在程序退出时自动清理
        globalThreadPool.reset();
    }
};

// 静态全局对象，确保在 main 函数之前初始化
static ThreadPoolInitializer g_threadPoolInitializer;

} // namespace memchainer
