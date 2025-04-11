#include "common/thread_pool.h"

namespace memchainer {

// 全局线程池实例
std::shared_ptr<ThreadPool> globalThreadPool;

ThreadPool::ThreadPool(size_t numThreads) {
    // 确保至少有一个线程
    numThreads = std::max<size_t>(1, numThreads);
    
    // 创建工作线程
    for (size_t i = 0; i < numThreads; ++i) {
        workers.emplace_back([this] {
            // 线程主循环
            while (true) {
                std::function<void()> task;
                
                {
                    std::unique_lock<std::mutex> lock(queueMutex);
                    
                    // 等待任务或停止信号
                    condition.wait(lock, [this] { 
                        return stop || !tasks.empty(); 
                    });
                    
                    // 如果线程池停止且没有任务，退出线程
                    if (stop && tasks.empty()) {
                        return;
                    }
                    
                    // 获取任务
                    task = std::move(tasks.front());
                    tasks.pop();
                }
                
                // 执行任务
                task();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        stop = true;
    }
    
    // 通知所有线程
    condition.notify_all();
    
    // 等待所有线程结束
    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::wait() {
    std::unique_lock<std::mutex> lock(activeTaskMutex);
    
    // 等待活动任务计数为0
    activeTaskCondition.wait(lock, [this] { 
        return activeTaskCount == 0; 
    });
}

// 初始化全局线程池
struct ThreadPoolInitializer {
    ThreadPoolInitializer() {
        globalThreadPool = std::make_shared<ThreadPool>();
    }
};

// 全局对象，确保线程池在程序启动时初始化
static ThreadPoolInitializer initializer;

} // namespace memchainer
