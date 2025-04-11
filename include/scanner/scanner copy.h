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
    
    // 扫描指针链
    std::shared_ptr<PointerChain> scanPointerChain(
        const std::vector<Address>& targetAddresses,
        const ScanOptions& options,
        const ProgressCallback& progressCb = nullptr);

    std::shared_ptr<PointerChain> scanPointerChain_(const std::vector<Address> &targetAddresses, 
                                                    const ScanOptions &options, 
                                                    const ProgressCallback &progressCb);

    std::pair<std::vector<std::vector<size_t>>, std::vector<std::vector<PointerDir *>>> buildPointerDirsTreeSafe(std::vector<std::vector<PointerDir>> &dirs, std::vector<PointerRange> &ranges);

    void extractChainsFromTreeSafe(std::shared_ptr<PointerChain> &chain, const std::vector<std::vector<size_t>> &counts, const std::vector<std::vector<PointerDir *>> &contents, const std::vector<PointerRange> &ranges, size_t maxChains);

    size_t findDirIndexForChainSafe(const std::vector<size_t> &levelCounts, size_t chainIndex);

    // 从文件加载扫描结果
    std::shared_ptr<PointerChain> loadFromFile(const std::string& filename);
    
    // 保存扫描结果到文件
    bool saveToFile(const std::shared_ptr<PointerChain>& chain, const std::string& filename);
    
    // 内存统计和诊断
    void printMemoryStatistics() const;
    size_t getMemoryUsage() const;
    
    // 高级搜索功能
    std::shared_ptr<PointerChain> findComplexPointerChains(
        Address targetAddress, 
        uint32_t maxDepth,
        Offset maxOffset,
        uint32_t minChainLength = 2);
    
    // 内存分析功能
    void analyzeMemoryStructure(Address baseAddress, MemorySize size);
    
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
            ptr->refCount = 0;
            
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

    // 快速指针扫描
    uint32_t fastScanPointers(const std::vector<Address>& targets, Offset maxOffset);


    // 设置缓存文件路径
    void setCachePath(const std::string& cachePath) {
        if (fileCache_) {
            fileCache_->initialize(cachePath);
        }
    }

    // 检查地址是否有效
    bool isValidAddress(Address addr);

private:
    // 搜索区域中的指针
    void searchPointers(const std::vector<PointerDir>& prevLevel, 
                       std::vector<PointerData*>& results,
                       Offset maxOffset, 
                       bool limitResults,
                       uint32_t resultLimit);
    
    // 在特定区域中搜索指针
    void searchPointersInRegion(const MemoryRegion* region,
                              const std::vector<PointerDir>& prevLevel,
                              std::vector<PointerData*>& results,
                              Offset maxOffset);
    
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

    // 过滤内存区域指针
    void filterPointersByRegion(
        std::vector<std::vector<PointerDir>>& dirs,
        std::vector<PointerRange>& ranges,
        std::vector<PointerData*>& pointers,
        int level);

    // 构建指针树
    ChainInfo<size_t> buildPointerDirsTree(
        std::vector<std::vector<PointerDir>>& dirs,
        std::vector<PointerRange>& ranges);
        
    // 合并数据到文件
    void integrateDataToFile(
        std::vector<std::vector<PointerDir*>>& contents,
        std::vector<PointerRange>& ranges,
        FILE* file);
        
    // 从树中提取链
    void extractChainsFromTree(
        std::shared_ptr<PointerChain>& chain,
        const std::vector<std::vector<size_t>>& counts,
        const std::vector<std::vector<PointerDir*>>& contents,
        const std::vector<PointerRange>& ranges);
        
    // 提取单条指针链
    void extractChain(
        std::shared_ptr<PointerChain>& chain,
        size_t chainIndex,
        const std::vector<std::vector<size_t>>& counts,
        const std::vector<std::vector<PointerDir*>>& contents,
        const std::vector<PointerRange>& ranges);
  
    
    // 打印指针链
    void printPointerChain( PointerChain* chain);
    

    

    
    // 打印指针目录树
    // 查找链索引对应的指针索引
    size_t findDirIndexForChain(
        const std::vector<size_t>& levelCounts, 
        size_t chainIndex);
        
    // 用于内存结构分析的辅助函数
    bool isLikelyArray(const uint8_t* data, size_t size, size_t elementSize);
    bool isLikelyString(const uint8_t* data, size_t size);
    
    // 快速扫描特定区域指针 (为fastScanPointers方法服务)
    void fastScanRegionForPointers(
        const MemoryRegion* region, 
        Address minValue, 
        Address maxValue, 
        std::mutex& resultsMutex);

    // 内存缓存
    // std::shared_ptr<MemoryCache> memoryCache_;
    
    // 文件缓存系统
    std::shared_ptr<FileCache> fileCache_;
    
    // 高效的内存区域扫描
    void scanRegionWithFileCache(
        const MemoryRegion* region,
        const std::vector<PointerDir>& prevLevel,
        std::vector<PointerData*>& results,
        Offset maxOffset,
        std::mutex& resultsMutex);
    

        
    // 高效指针关联
    void buildOptimizedLevelAssociations(
        const std::vector<PointerDir>& prevLevel,
        std::vector<PointerDir>& currLevel,
        Offset maxOffset);

    std::shared_ptr<MemoryAccess> memoryAccess_;
    std::shared_ptr<MemoryMap> memoryMap_;
    std::vector<PointerData*> pointerCache_;
    std::unordered_map<Address, PointerData*> addressMap_;
    PointerDataPool dataPool_;
};

} // namespace memchainer
