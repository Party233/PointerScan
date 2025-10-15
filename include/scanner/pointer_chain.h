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

    void CalPointerChainMem( std::vector<std::vector<PointerRange>>& dirs);        
    // 构建指针链
    void buildPointerChain(std::vector<std::vector<PointerRange>>& dirs);

    void printChain();

    // 获取总链数
    size_t getTotalChains() const { return totalChains_; }



    // 清空所有数据
    void clear();

    // 序列化到二进制数据
    std::vector<uint8_t> serialize() const;

    // 从二进制数据反序列化
    static std::shared_ptr<PointerChain> deserialize(const std::vector<uint8_t>& data);

    // 检查是否为空
    bool isEmpty() const { return chains_.empty(); }

    // 获取指针链数据（用于格式化输出）
    const std::vector<std::list<PointerChainNode>>& getChains() const { return chains_; }

    // 优化内存使用
    void optimizeMemoryUsage();

private:
    // 存储所有指针链
    // 使用双向链表存储，便于查找
    std::vector<std::list<PointerChainNode>> chains_;


    // 最大层级
    size_t maxLevel_ = 0;
    
    // 总链数
    size_t totalChains_ = 0;
    
    // 压缩标志
    bool isCompressed_ = false;
};

} // namespace memchainer
