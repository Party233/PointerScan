#include "memory/memory_cache.h"
#include <algorithm>
#include <chrono>
#include <cstring>

namespace memchainer {

MemoryCache::MemoryCache(size_t blockSize)
    : blockSize_(blockSize), maxCacheBlocks_(64),
      cacheHits_(0), cacheMisses_(0) {
}

MemoryCache::~MemoryCache() {
    clear();
}

bool MemoryCache::readMemory(const std::shared_ptr<MemoryAccess>& memAccess, 
                           Address address, void* buffer, size_t size) {
    // 检查边界情况
    if (!buffer || size == 0) {
        return false;
    }
    
    // 如果读取跨越多个块，分块处理
    if (getBlockStartAddress(address) != getBlockStartAddress(address + size - 1)) {
        // 第一个块
        Address firstBlockEnd = getBlockStartAddress(address) + blockSize_;
        size_t firstPartSize = firstBlockEnd - address;
        
        // 读取第一个块
        if (!readMemory(memAccess, address, buffer, firstPartSize)) {
            return false;
        }
        
        // 读取剩余部分
        return readMemory(memAccess, 
                        firstBlockEnd, 
                        static_cast<uint8_t*>(buffer) + firstPartSize, 
                        size - firstPartSize);
    }
    
    // 计算块地址
    Address blockStart = getBlockStartAddress(address);
    
    // 尝试从缓存读取
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        
        auto it = cacheBlocks_.find(blockStart);
        if (it != cacheBlocks_.end() && it->second.isValid) {
            // 缓存命中
            cacheHits_++;
            
            // 更新访问时间
            it->second.lastAccess = std::chrono::steady_clock::now().time_since_epoch().count();
            
            // 计算偏移量并复制数据
            size_t offset = static_cast<size_t>(address - blockStart);
            if (offset + size <= it->second.data.size()) {
                memcpy(buffer, it->second.data.data() + offset, size);
                return true;
            }
        }
    }
    
    // 缓存未命中，读取一个完整块
    cacheMisses_++;
    
    // 创建新缓存块
    CacheBlock newBlock;
    newBlock.startAddress = blockStart;
    newBlock.endAddress = blockStart + blockSize_;
    newBlock.data.resize(blockSize_);
    newBlock.lastAccess = std::chrono::steady_clock::now().time_since_epoch().count();
    
    // 尝试读取整个块
    std::error_code ec;
    newBlock.isValid = memAccess->read(blockStart, newBlock.data.data(), blockSize_, ec);
    
    if (newBlock.isValid) {
        // 计算偏移量并复制数据
        size_t offset = static_cast<size_t>(address - blockStart);
        memcpy(buffer, newBlock.data.data() + offset, size);
        
        // 添加到缓存
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            
            // 检查缓存是否已满
            if (cacheBlocks_.size() >= maxCacheBlocks_) {
                evictOldestBlock();
            }
            
            cacheBlocks_[blockStart] = std::move(newBlock);
        }
        
        return true;
    }
    
    // 无法读取整个块，尝试直接读取所需数据
    return memAccess->read(address, buffer, size, ec);
}

bool MemoryCache::preloadRegion(const std::shared_ptr<MemoryAccess>& memAccess,
                              Address startAddress, Address endAddress) {
    if (startAddress >= endAddress) {
        return false;
    }
    
    // 计算要加载的块数量
    Address firstBlock = getBlockStartAddress(startAddress);
    Address lastBlock = getBlockStartAddress(endAddress - 1);
    
    bool allSuccess = true;
    
    // 逐块加载
    for (Address blockStart = firstBlock; blockStart <= lastBlock; blockStart += blockSize_) {
        // 检查是否已缓存
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            
            if (cacheBlocks_.find(blockStart) != cacheBlocks_.end()) {
                continue; // 已缓存，跳过
            }
        }
        
        // 创建新缓存块
        CacheBlock newBlock;
        newBlock.startAddress = blockStart;
        newBlock.endAddress = blockStart + blockSize_;
        newBlock.data.resize(blockSize_);
        newBlock.lastAccess = std::chrono::steady_clock::now().time_since_epoch().count();
        
        // 读取整个块
        std::error_code ec;
        newBlock.isValid = memAccess->read(blockStart, newBlock.data.data(), blockSize_, ec);
        
        if (newBlock.isValid) {
            // 添加到缓存
            std::lock_guard<std::mutex> lock(cacheMutex_);
            
            // 检查缓存是否已满
            if (cacheBlocks_.size() >= maxCacheBlocks_) {
                evictOldestBlock();
            }
            
            cacheBlocks_[blockStart] = std::move(newBlock);
        } else {
            allSuccess = false;
        }
    }
    
    return allSuccess;
}

void MemoryCache::clear() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    cacheBlocks_.clear();
    cacheHits_ = 0;
    cacheMisses_ = 0;
}

double MemoryCache::getHitRatio() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    size_t total = cacheHits_ + cacheMisses_;
    if (total == 0) {
        return 0.0;
    }
    
    return static_cast<double>(cacheHits_) / total;
}

void MemoryCache::evictOldestBlock() {
    if (cacheBlocks_.empty()) {
        return;
    }
    
    // 查找最旧的块
    auto oldest = cacheBlocks_.begin();
    for (auto it = std::next(oldest); it != cacheBlocks_.end(); ++it) {
        if (it->second.lastAccess < oldest->second.lastAccess) {
            oldest = it;
        }
    }
    
    cacheBlocks_.erase(oldest);
}

Address MemoryCache::getBlockStartAddress(Address address) const {
    return (address / blockSize_) * blockSize_;
}

} // namespace memchainer
