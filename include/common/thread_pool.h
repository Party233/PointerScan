#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <atomic>
#include <stdexcept>

namespace memchainer {

/**
 * @brief 高性能线程池实现
 * 
 * 特性：
 * - 支持任意可调用对象（函数、lambda、成员函数等）
 * - 自动管理线程生命周期
 * - 支持等待所有任务完成
 * - 线程安全
 * - 异常安全
 */
class ThreadPool {
public:
    /**
     * @brief 构造函数
     * @param numThreads 线程数量，默认为硬件并发数
     */
    explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency());
    
    /**
     * @brief 析构函数，自动等待所有任务完成并清理资源
     */
    ~ThreadPool();
    
    // 禁止拷贝和移动
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;
    
    /**
     * @brief 提交任务到线程池
     * @tparam F 可调用对象类型
     * @tparam Args 参数类型
     * @param f 可调用对象
     * @param args 参数
     * @return std::future 用于获取任务返回值
     * @throws std::runtime_error 如果线程池已停止
     */
    template<class F, class... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>>;
    
    /**
     * @brief 等待所有任务完成（包括正在执行和队列中的任务）
     */
    void wait();
    
    /**
     * @brief 获取线程数量
     */
    size_t size() const noexcept { return workers.size(); }
    
    /**
     * @brief 获取待处理任务数量
     */
    size_t pendingTasks() const noexcept { return pendingTaskCount.load(std::memory_order_relaxed); }
    
    /**
     * @brief 获取正在执行的任务数量
     */
    size_t activeTasks() const noexcept { return activeTaskCount.load(std::memory_order_relaxed); }
    
    /**
     * @brief 检查线程池是否已停止
     */
    bool isStopped() const noexcept { return stop.load(std::memory_order_relaxed); }

private:
    // 工作线程容器
    std::vector<std::thread> workers;
    
    // 任务队列
    std::queue<std::function<void()>> tasks;
    
    // 任务队列同步
    mutable std::mutex queueMutex;
    std::condition_variable queueCondition;
    
    // 任务计数器（原子操作，提高性能）
    std::atomic<size_t> pendingTaskCount{0};  // 队列中的任务
    std::atomic<size_t> activeTaskCount{0};   // 正在执行的任务
    
    // 等待所有任务完成的同步
    mutable std::mutex completionMutex;
    std::condition_variable completionCondition;
    
    // 线程池状态
    std::atomic<bool> stop{false};
    
    // 工作线程主函数
    void workerThread();
};

// 全局线程池实例
extern std::shared_ptr<ThreadPool> globalThreadPool;

// ============================================================================
// 模板方法实现
// ============================================================================

template<class F, class... Args>
auto ThreadPool::submit(F&& f, Args&&... args) 
    -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>> 
{
    using return_type = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;
    
    // 检查线程池状态
    if (stop.load(std::memory_order_relaxed)) {
        throw std::runtime_error("无法向已停止的线程池提交任务");
    }
    
    // 创建打包任务
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    
    // 获取 future 对象
    std::future<return_type> result = task->get_future();
    
    // 将任务添加到队列
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        
        // 再次检查（双重检查锁定模式）
        if (stop.load(std::memory_order_relaxed)) {
            throw std::runtime_error("无法向已停止的线程池提交任务");
        }
        
        // 包装任务以处理计数
        tasks.emplace([this, task]() {
            // 任务开始执行
            pendingTaskCount.fetch_sub(1, std::memory_order_relaxed);
            activeTaskCount.fetch_add(1, std::memory_order_relaxed);
            
            try {
                // 执行任务
                (*task)();
            } catch (...) {
                // 确保即使任务抛出异常，计数器也能正确更新
            }
            
            // 任务执行完成
            activeTaskCount.fetch_sub(1, std::memory_order_relaxed);
            
            // 如果所有任务都完成了，通知等待的线程
            if (pendingTaskCount.load(std::memory_order_relaxed) == 0 && 
                activeTaskCount.load(std::memory_order_relaxed) == 0) {
                completionCondition.notify_all();
            }
        });
        
        pendingTaskCount.fetch_add(1, std::memory_order_relaxed);
    }
    
    // 唤醒一个工作线程
    queueCondition.notify_one();
    
    return result;
}

} // namespace memchainer
