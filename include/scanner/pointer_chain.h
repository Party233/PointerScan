#pragma once

#include "common/types.h"
#include <vector>
#include <memory>
#include <string>

namespace memchainer {

// 定义指针链类
class PointerChain {
public:
    PointerChain();
    ~PointerChain();

    // 添加一个新层级的节点
    void addLevel(uint32_t level);

    // 添加指针数据到指定层级
    void addPointerData(uint32_t level, const PointerData& data);

    // 获取指定层级的所有指针
    const std::vector<PointerData>& getPointersAtLevel(uint32_t level) const;

    // 获取总层数
    uint32_t getLevelCount() const;

    // 获取总指针链数量
    uint32_t getChainCount() const;

    // 获取特定链的偏移量
    std::vector<Offset> getChainOffsets(uint32_t chainIndex) const;

    // 检查是否为空
    bool isEmpty() const;

    // 序列化到二进制数据
    std::vector<uint8_t> serialize() const;

    // 从二进制数据反序列化
    static std::shared_ptr<PointerChain> deserialize(const std::vector<uint8_t>& data);

    // 添加预分配空间的方法
    void reservePointers(uint32_t level, size_t count);
    
    // 优化内存使用方法
    void optimizeMemoryUsage();
    
    // 数据压缩和解压缩方法
    void compressData();
    void decompressData();
    
    // 检查是否已压缩
    bool isCompressed() const { return isCompressed_; }
    
    // 提取前N条链
    std::shared_ptr<PointerChain> extractTopChains(uint32_t count) const;

private:
    // 存储每个层级的指针数据
    std::vector<ChainNode> levels_;
    
    // 缓存的链计数
    mutable uint32_t cachedChainCount_;
    mutable bool isCountValid_;
    
    // 计算链数量
    void recalculateChainCount() const;

    // 压缩数据结构
    struct CompressedLevel {
        Address baseAddress;
        std::vector<uint32_t> addressOffsets;
        std::vector<Offset> valueOffsets;
        std::vector<uint32_t> refCounts;
    };
    
    std::vector<CompressedLevel> compressedData_;
    bool isCompressed_ = false;
};

} // namespace memchainer
