#pragma once

#include "memory/mem_access.h"
#include "memory/mem_map.h"
#include "memory/file_cache.h"
#include "scanner/pointer_chain.h"
#include <functional>
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace memchainer {

class PointerScanner {
public:
    // 配置选项
    struct ScanOptions {
        uint32_t maxDepth = 10;      // 最大指针链深度
        Offset maxOffset = 500;      // 最大允许的偏移量
        bool limitResults = false;   // 是否限制结果数量
        uint32_t resultLimit = 1000; // 结果数量限制
        uint32_t batchSize = 10000;  // 处理批次大小
        uint32_t threadCount = 4;    // 线程数量
    };

    // 进度回调函数类型
    using ProgressCallback = std::function<void(uint32_t level, uint32_t totalLevels, float progress)>;

    PointerScanner();
    ~PointerScanner();

    // 初始化扫描器
    bool initialize(const std::shared_ptr<MemoryAccess>& memAccess, 
                    const std::shared_ptr<MemoryMap>& memMap);

    // 查找指针
    uint32_t findPointers(Address startAddress, Address endAddress);
    
    // 扫描特定区域内的指针
    void scanRegionForPointers(Address startAddress, Address endAddress);
        // 过滤内存区域指针
    void Search1Pointers(
        std::vector<std::vector<PointerRange>>& dirs,
        std::vector<PointerRange>& ranges,
        PointerData* pointers,
        const ScanOptions& options
        );
    // 扫描指针链
    std::shared_ptr<PointerChain> scanPointerChain(
        Address& targetAddress,
        const ScanOptions& options,
        const ProgressCallback& progressCb = nullptr);

    // 检查值是否指向前一层的某个指针
    bool isPointerToPrevLevel(Address value, 
                            const std::vector<PointerDir>& prevLevel,
                            Offset maxOffset);
    
    // 计算偏移量
    Offset calculateOffset(Address value, 
                         const std::vector<PointerDir>& prevLevel);
    
    // 关联指针索引
    void associatePointerIndices(const std::vector<PointerDir>& prevLevel, 
                               std::vector<PointerDir>& currLevel, 
                               Offset maxOffset);

    // 判断地址是否在静态区域内
    StaticOffset calculateStaticOffset(Address addr);

    // 构建指针树
    ChainInfo<size_t> buildPointerDirsTree(
        std::vector<std::vector<PointerRange>>& dirs
       );

    // 打印所有有效的指针链
    void printPointerChains(const ChainInfo<size_t>& chainInfo);
    

    // 从文件加载扫描结果
    std::shared_ptr<PointerChain> loadFromFile(const std::string& filename);
    
    // 保存扫描结果到文件
    bool saveToFile(const std::shared_ptr<PointerChain>& chain, const std::string& filename);
    

    // 使用对象池减少内存分配开销
    class PointerDataPool {
    public:
        PointerDataPool(size_t initialSize = 10000) {
            pool_.reserve(initialSize);
        }
        
        PointerData* allocate(Address addr, Address val, Offset off = 0) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pool_.empty()) {
                return new PointerData(addr, val, off);
            }
            
            PointerData* ptr = pool_.back();
            pool_.pop_back();
            
            ptr->address = addr;
            ptr->value = val;
            ptr->offset = off;
           
            
            return ptr;
        }
        
        void release(PointerData* ptr) {
            if (!ptr) return;
            
            std::lock_guard<std::mutex> lock(mutex_);
            pool_.push_back(ptr);
        }
        
        void clear() {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto* ptr : pool_) {
                delete ptr;
            }
            pool_.clear();
        }
        
    private:
        std::vector<PointerData*> pool_;
        std::mutex mutex_;
    };

    // 设置缓存文件路径
    void setCachePath(const std::string& cachePath) {
        if (fileCache_) {
            fileCache_->initialize(cachePath);
        }
    }

    // 检查地址是否有效
    bool isValidAddress(Address& addr);

private:

    
    // 打印指针链
    void printPointerChain( PointerChain* chain);
    
    // 内存缓存
    // std::shared_ptr<MemoryCache> memoryCache_;
    
    // 文件缓存系统
    std::shared_ptr<FileCache> fileCache_;
    std::shared_ptr<MemoryAccess> memoryAccess_;
    std::shared_ptr<MemoryMap> memoryMap_;
    std::vector<PointerAllData*> pointerCache_;
    std::unordered_map<Address, PointerData*> addressMap_;
    PointerDataPool dataPool_;
};

} // namespace memchainer
