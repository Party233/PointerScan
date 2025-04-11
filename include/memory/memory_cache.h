#pragma once

#include "common/types.h"
#include "memory/mem_access.h"
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>

namespace memchainer {

class MemoryCache {
public:
    struct CacheBlock {
        Address startAddress;
        Address endAddress;
        std::vector<uint8_t> data;
        bool isValid;
        uint64_t lastAccess; // 最后访问时间戳
    };

    MemoryCache(size_t blockSize = 1024 * 1024);
    ~MemoryCache();

    // 从缓存读取内存数据
    bool readMemory(const std::shared_ptr<MemoryAccess>& memAccess, 
                  Address address, void* buffer, size_t size);
    
    // 预加载内存区域到缓存
    bool preloadRegion(const std::shared_ptr<MemoryAccess>& memAccess,
                     Address startAddress, Address endAddress);
    
    // 清除缓存
    void clear();
    
    // 获取性能统计
    size_t getCacheHits() const { return cacheHits_; }
    size_t getCacheMisses() const { return cacheMisses_; }
    double getHitRatio() const;
    
    // 设置最大缓存大小
    void setMaxCacheSize(size_t maxBlocks) { maxCacheBlocks_ = maxBlocks; }

private:
    size_t blockSize_;
    std::unordered_map<Address, CacheBlock> cacheBlocks_; // 使用map提高查找效率
    size_t maxCacheBlocks_;
    
    // 性能统计
    size_t cacheHits_;
    size_t cacheMisses_;
    
    // 同步
    mutable std::mutex cacheMutex_;
    
    // 内部方法
    void evictOldestBlock();
    Address getBlockStartAddress(Address address) const;
};

} // namespace memchainer
