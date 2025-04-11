#include "scanner/pointer_chain.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include "pointer_chain.h"

namespace memchainer {

PointerChain::PointerChain() : maxLevel_(0), totalChains_(0), isCompressed_(false) {}

PointerChain::~PointerChain() {
    clear();
}

void PointerChain::buildPointerChain( std::vector<std::vector<PointerRange>>& dirs) {
    // 清空现有数据
    clear();

    // 检查输入有效性
    if (dirs.empty()) {
        std::cout << "没有找到指针链" << std::endl;
        return;
    }
    std::cout << "开始构建指针链" << std::endl;
    // 初始化基本信息
    maxLevel_ = dirs.size() ;
    std::vector<PointerDir> staticPointers;

    // 先查找静态指针
    for (size_t level = 0; level < maxLevel_; ++level) {
        if(dirs[level].empty()) {
            continue;
        }
        for (const auto& range : dirs[level]) {
            for (const auto& dir : range.results) {
                if (dir.staticOffset_.staticOffset != 0) {
                    // 找到静态指针
                    staticPointers.push_back(dir);
                }
            }
        }
    }

    if (staticPointers.empty()) {
        std::cout << "没有找到静态指针" << std::endl;
        return;
    }
    std::cout << "找到 " << staticPointers.size() << " 个静态指针" << std::endl;
    // 从静态指针开始构建指针链
    for ( auto& dir : staticPointers) {
        std::list<PointerChainNode> chain;
        PointerChainNode node(dir.address, dir.value, dir.offset, dir.staticOffset_);
        chain.push_back(node);
        // 从静态指针开始构建指针链
        while (dir.child != nullptr) {
            PointerChainNode childnode(dir.child->address,
             dir.child->value, dir.child->offset, dir.child->staticOffset_);
            chain.push_back(childnode);
            dir = *dir.child;
        }
        chains_.push_back(std::move(chain));
    }

    // 打印统计信息
    printChain();
}


void PointerChain::printChain() {
    for (auto& chain : chains_) {
        // 打印静态头
        std::cout << std::hex << "static head: " << chain.front().address 
        << " value: " << chain.front().value 
        << " offset:0x" << -chain.front().offset 
        << " staticOffset:0x" << chain.front().staticOffset.staticOffset
        << " region: " << chain.front().staticOffset.region->name << std::endl;
        //弹出静态头
        chain.pop_front();

        for (auto& node : chain) {
            std::cout << "address: " << node.address 
            << " ->value: " << node.value 
            << " offset: " << -node.offset;
            std::cout << std::endl;
            
        }
        
    }
}




void PointerChain::clear() {
    chains_.clear();
    maxLevel_ = 0;
    totalChains_ = 0;
    isCompressed_ = false;
}

std::vector<uint8_t> PointerChain::serialize() const {
    // TODO: 实现序列化
    return std::vector<uint8_t>();
}

std::shared_ptr<PointerChain> PointerChain::deserialize(const std::vector<uint8_t>& data) {
    // TODO: 实现反序列化
    return std::make_shared<PointerChain>();
}


void PointerChain::optimizeMemoryUsage() {
   
    chains_.shrink_to_fit();
}

} // namespace memchainer
