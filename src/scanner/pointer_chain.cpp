#include "scanner/pointer_chain.h"
#include <algorithm>
#include <cassert>
#include <fstream>

namespace memchainer {

PointerChain::PointerChain() 
    : cachedChainCount_(0), isCountValid_(false) {
}

PointerChain::~PointerChain() = default;

void PointerChain::addLevel(uint32_t level) {
    // 确保层级索引连续
    if (level >= levels_.size()) {
        levels_.resize(level + 1);
    }
    
    // 设置层级编号
    levels_[level].level = level;
    
    // 添加新层级后，链计数缓存失效
    isCountValid_ = false;
}

void PointerChain::addPointerData(uint32_t level, const PointerData& data) {
    // 确保层级存在
    if (level >= levels_.size()) {
        addLevel(level);
    }
    
    // 添加指针数据
    levels_[level].pointers.push_back(data);
    
    // 添加新数据后，链计数缓存失效
    isCountValid_ = false;
}

const std::vector<PointerData>& PointerChain::getPointersAtLevel(uint32_t level) const {
    static const std::vector<PointerData> emptyVector;
    
    if (level < levels_.size()) {
        return levels_[level].pointers;
    }
    
    return emptyVector;
}

uint32_t PointerChain::getLevelCount() const {
    return static_cast<uint32_t>(levels_.size());
}

uint32_t PointerChain::getChainCount() const {
    if (!isCountValid_) {
        recalculateChainCount();
    }
    return cachedChainCount_;
}

void PointerChain::recalculateChainCount() const {
    if (levels_.empty()) {
        cachedChainCount_ = 0;
        isCountValid_ = true;
        return;
    }
    
    // 通常最后一层的指针数量就是链数量
    // 这里假设每个链都完整到达最后一层
    cachedChainCount_ = static_cast<uint32_t>(levels_.back().pointers.size());
    isCountValid_ = true;
}

std::vector<Offset> PointerChain::getChainOffsets(uint32_t chainIndex) const {
    std::vector<Offset> offsets;
    
    if (levels_.empty() || chainIndex >= getChainCount()) {
        return offsets;
    }
    
    // 从最后一层开始往回追溯
    uint32_t currentLevel = static_cast<uint32_t>(levels_.size() - 1);
    offsets.reserve(currentLevel);
    
    // 提取每一层的偏移量
    while (currentLevel > 0) {
        const auto& currentPointer = levels_[currentLevel].pointers[chainIndex];
        offsets.push_back(currentPointer.offset);
        currentLevel--;
    }
    
    // 翻转偏移量顺序，使其从基址到目标
    std::reverse(offsets.begin(), offsets.end());
    return offsets;
}

bool PointerChain::isEmpty() const {
    return levels_.empty() || levels_[0].pointers.empty();
}

std::vector<uint8_t> PointerChain::serialize() const {
    std::vector<uint8_t> result;
    
    // 计算所需空间
    size_t totalSize = sizeof(uint32_t); // 层数
    
    // 计算每层的大小
    for (const auto& level : levels_) {
        totalSize += sizeof(uint32_t); // 层级号
        totalSize += sizeof(uint32_t); // 指针数量
        totalSize += level.pointers.size() * sizeof(PointerData); // 所有指针数据
    }
    
    // 分配空间
    result.reserve(totalSize);
    
    // 写入层数
    uint32_t levelCount = static_cast<uint32_t>(levels_.size());
    result.insert(result.end(), 
                reinterpret_cast<const uint8_t*>(&levelCount),
                reinterpret_cast<const uint8_t*>(&levelCount) + sizeof(levelCount));
    
    // 写入每层数据
    for (const auto& level : levels_) {
        // 写入层级号
        result.insert(result.end(),
                    reinterpret_cast<const uint8_t*>(&level.level),
                    reinterpret_cast<const uint8_t*>(&level.level) + sizeof(level.level));
        
        // 写入指针数量
        uint32_t pointerCount = static_cast<uint32_t>(level.pointers.size());
        result.insert(result.end(),
                    reinterpret_cast<const uint8_t*>(&pointerCount),
                    reinterpret_cast<const uint8_t*>(&pointerCount) + sizeof(pointerCount));
        
        // 写入指针数据
        for (const auto& pointer : level.pointers) {
            result.insert(result.end(),
                        reinterpret_cast<const uint8_t*>(&pointer),
                        reinterpret_cast<const uint8_t*>(&pointer) + sizeof(pointer));
        }
    }
    
    return result;
}

std::shared_ptr<PointerChain> PointerChain::deserialize(const std::vector<uint8_t>& data) {
    auto chain = std::make_shared<PointerChain>();
    
    if (data.size() < sizeof(uint32_t)) {
        return chain; // 返回空链
    }
    
    size_t offset = 0;
    
    // 读取层数
    uint32_t levelCount = *reinterpret_cast<const uint32_t*>(data.data() + offset);
    offset += sizeof(uint32_t);
    
    // 读取每层数据
    for (uint32_t i = 0; i < levelCount; ++i) {
        // 确保有足够的数据
        if (offset + sizeof(uint32_t) * 2 > data.size()) {
            break;
        }
        
        // 读取层级号
        uint32_t level = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        
        // 读取指针数量
        uint32_t pointerCount = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        
        // 添加层级
        chain->addLevel(level);
        
        // 读取指针数据
        for (uint32_t j = 0; j < pointerCount; ++j) {
            // 确保有足够的数据
            if (offset + sizeof(PointerData) > data.size()) {
                break;
            }
            
            // 读取指针数据
            const PointerData* pointer = reinterpret_cast<const PointerData*>(data.data() + offset);
            offset += sizeof(PointerData);
            
            // 添加指针数据
            chain->addPointerData(level, *pointer);
        }
    }
    
    return chain;
}

void PointerChain::optimizeMemoryUsage() {
    for (auto& level : levels_) {
        level.optimizeMemory();
    }
    
    // 如果有过多空层级，移除它们
    size_t lastNonEmptyLevel = 0;
    for (size_t i = 0; i < levels_.size(); ++i) {
        if (!levels_[i].pointers.empty()) {
            lastNonEmptyLevel = i;
        }
    }
    
    if (lastNonEmptyLevel + 1 < levels_.size()) {
        levels_.resize(lastNonEmptyLevel + 1);
    }
    
    isCountValid_ = false;
}

void PointerChain::compressData() {
    if (isCompressed_) return;
    
    // 创建压缩数据
    std::vector<PointerChain::CompressedLevel> compressedLevels;
    compressedLevels.resize(levels_.size());
    
    for (size_t levelIndex = 0; levelIndex < levels_.size(); ++levelIndex) {
        auto& level = levels_[levelIndex];
        auto& compressed = compressedLevels[levelIndex];
        
        if (level.pointers.empty()) continue;
        
        // 排序指针
        level.sortPointers();
        
        // 保存第一个指针的地址作为基准
        compressed.baseAddress = level.pointers[0].address;
        
        // 预分配空间
        size_t pointersCount = level.pointers.size();
        compressed.addressOffsets.reserve(pointersCount - 1);
        compressed.valueOffsets.reserve(pointersCount);
        //compressed.refCounts.reserve(pointersCount);
        
        // 保存第一个指针的偏移和引用计数
        compressed.valueOffsets.push_back(level.pointers[0].offset);
        //compressed.refCounts.push_back(level.pointers[0].refCount);
        
        // 压缩剩余指针
        Address previousAddr = level.pointers[0].address;
        for (size_t i = 1; i < pointersCount; ++i) {
            // 计算与前一个地址的差值
            uint32_t addrDiff = static_cast<uint32_t>(level.pointers[i].address - previousAddr);
            compressed.addressOffsets.push_back(addrDiff);
            
            // 保存偏移量和引用计数
            compressed.valueOffsets.push_back(level.pointers[i].offset);
            //compressed.refCounts.push_back(level.pointers[i].refCount);
            
            previousAddr = level.pointers[i].address;
        }
        
        // 释放原始数据以节省内存
        std::vector<PointerData> temp;
        temp.emplace_back(compressed.baseAddress, 0, compressed.valueOffsets[0]);
        level.pointers.swap(temp);
    }
    
    // 保存压缩数据到成员变量
    compressedData_ = std::move(compressedLevels);
    isCompressed_ = true;
}

void PointerChain::decompressData() {
    if (!isCompressed_) return;
    
    for (size_t levelIndex = 0; levelIndex < levels_.size(); ++levelIndex) {
        auto& level = levels_[levelIndex];
        auto& compressed = compressedData_[levelIndex];
        
        if (compressed.addressOffsets.empty() && level.pointers.size() <= 1) {
            // 这一层没有被压缩或者只有一个指针
            continue;
        }
        
        // 计算总指针数
        size_t totalPointers = compressed.addressOffsets.size() + 1;
        
        // 重建完整的指针列表
        std::vector<PointerData> rebuiltPointers;
        rebuiltPointers.reserve(totalPointers);
        
        // 添加第一个指针
        rebuiltPointers.emplace_back(compressed.baseAddress, 0, 
                                   compressed.valueOffsets[0]);
        
        // 重建其余指针
        Address currentAddr = compressed.baseAddress;
        for (size_t i = 0; i < compressed.addressOffsets.size(); ++i) {
            currentAddr += compressed.addressOffsets[i];
            rebuiltPointers.emplace_back(currentAddr, 0, 
                                       compressed.valueOffsets[i + 1]);
        }
        
        // 替换当前层的指针
        level.pointers.swap(rebuiltPointers);
    }
    
    // 清除压缩数据
    compressedData_.clear();
    isCompressed_ = false;
}

void PointerChain::reservePointers(uint32_t level, size_t count) {
    if (level >= levels_.size()) {
        addLevel(level);
    }
    
    levels_[level].pointers.reserve(count);
}

std::shared_ptr<PointerChain> PointerChain::extractTopChains(uint32_t count) const {
    auto result = std::make_shared<PointerChain>();
    
    if (isEmpty() || count == 0) {
        return result;
    }
    
    // 获取实际可提取的链数量
    uint32_t chainCount = getChainCount();
    count = std::min(count, chainCount);
    
    // 为每个层级预分配空间
    for (uint32_t level = 0; level < levels_.size(); ++level) {
        result->addLevel(level);
        result->reservePointers(level, count);
    }
    
    // 复制前count条链的数据
    for (uint32_t level = 0; level < levels_.size(); ++level) {
        const auto& levelPointers = levels_[level].pointers;
        
        // 确保有足够的指针
        if (level < levels_.size() - 1 || levelPointers.size() <= count) {
            // 复制全部指针或前count个
            for (uint32_t i = 0; i < std::min(count, static_cast<uint32_t>(levelPointers.size())); ++i) {
                result->addPointerData(level, levelPointers[i]);
            }
        } else {
            // 最后一层可能有更多指针，只取前count个
            for (uint32_t i = 0; i < count; ++i) {
                result->addPointerData(level, levelPointers[i]);
            }
        }
    }
    
    return result;
}

} // namespace memchainer
