#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>

namespace memchainer {

class ThreadPool {
public:
    ThreadPool(size_t numThreads = std::thread::hardware_concurrency());
    ~ThreadPool();
    
    // 禁止拷贝和赋值
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    
    // 提交任务到线程池
    template<class F, class... Args>
    auto submit(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>;
    
    // 等待所有任务完成
    void wait();
    
    // 获取线程数量
    size_t size() const { return workers.size(); }

private:
    // 工作线程
    std::vector<std::thread> workers;
    
    // 任务队列
    std::queue<std::function<void()>> tasks;
    
    // 同步对象
    std::mutex queueMutex;
    std::condition_variable condition;
    
    // 活动任务计数
    size_t activeTaskCount = 0;
    std::mutex activeTaskMutex;
    std::condition_variable activeTaskCondition;
    
    // 线程池控制标志
    bool stop = false;
};

// 全局线程池实例
extern std::shared_ptr<ThreadPool> globalThreadPool;

template<class F, class... Args>
auto ThreadPool::submit(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> {
    using return_type = typename std::result_of<F(Args...)>::type;
    
    // 创建任务包装器
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    
    // 获取future用于返回结果
    std::future<return_type> result = task->get_future();
    
    {
        // 增加活动任务计数
        std::lock_guard<std::mutex> activeLock(activeTaskMutex);
        ++activeTaskCount;
    }
    
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        
        // 如果线程池已停止，抛出异常
        if (stop) {
            throw std::runtime_error("线程池已停止，无法提交任务");
        }
        
        // 添加任务到队列
        tasks.emplace([this, task]() {
            (*task)();
            
            // 减少活动任务计数
            std::lock_guard<std::mutex> activeLock(activeTaskMutex);
            --activeTaskCount;
            
            // 如果没有活动任务，通知等待的线程
            if (activeTaskCount == 0) {
                activeTaskCondition.notify_all();
            }
        });
    }
    
    // 通知一个等待中的线程有新任务
    condition.notify_one();
    
    return result;
}

} // namespace memchainer
