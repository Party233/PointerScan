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
    uint32_t findPointers();
    
    // 扫描特定区域内的指针
    void scanRegionForPointers(Address startAddress, Address endAddress);
        // 过滤内存区域指针
    void Search1Pointers(
        std::vector<std::vector<PointerRange>>& dirs,
        PointerData* pointers,
        const ScanOptions& options
        );
    // 扫描指针链
    int scanPointerChain(
        Address& targetAddress,
        const ScanOptions& options,
        const ProgressCallback& progressCb = nullptr);


    // 判断地址是否在静态区域内
    StaticOffset calculateStaticOffset(Address addr);

    // 检查地址是否有效
    bool isValidAddress(Address& addr);

    // 获取指针链
    const std::vector<std::list<PointerChainNode>>& getChains() const { return chains_; }

private:

    // 文件缓存系统
    //std::shared_ptr<FileCache> fileCache_;
    std::shared_ptr<MemoryAccess> memoryAccess_;
    std::shared_ptr<MemoryMap> memoryMap_;
    std::vector<PointerAllData*> pointerCache_;
    //std::unordered_map<Address, PointerData*> addressMap_;
    //PointerDataPool dataPool_;

            // 存储所有指针链
    // 使用双向链表存储，便于查找
    std::vector<std::list<PointerChainNode>> chains_;


};

} // namespace memchainer
