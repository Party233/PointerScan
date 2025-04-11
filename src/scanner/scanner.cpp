#include <algorithm>
#include <chrono>
#include <iostream>
#include <fstream>
#include <cstring>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <iomanip>
#include <future>
#include <cmath>
#include <map>

#include "common/types.h"
#include "common/thread_pool.h" 
// #include "memory/memory_cache.h"  
#include "scanner.h"

#include <sys/types.h>

namespace memchainer {

PointerScanner::PointerScanner() {
    // 创建文件缓存
    fileCache_ = std::make_shared<FileCache>();
    fileCache_->initialize();
}

PointerScanner::~PointerScanner() {
    // 清理指针缓存
    for (auto* ptr : pointerCache_) {
        delete ptr;
    }
    pointerCache_.clear();
    addressMap_.clear();
}

bool PointerScanner::initialize(const std::shared_ptr<MemoryAccess>& memAccess, 
                             const std::shared_ptr<MemoryMap>& memMap) {
    if (!memAccess || !memMap) {
        return false;
    }
    
    memoryAccess_ = memAccess;
    memoryMap_ = memMap;
    return true;
}

uint32_t PointerScanner::findPointers(Address startAddress, Address endAddress) {

    // 清理旧指针
    for (auto* ptr : pointerCache_) {
        delete ptr;
    }
    pointerCache_.clear();
    addressMap_.clear();
    
    // 获取过滤的内存区域
    auto regions = memoryMap_->getFilteredRegions();
    
    // 如果没有指定范围，扫描所有过滤的区域
    if (startAddress == 0 && endAddress == 0) {
        for (const auto* region : regions) {
            //线程池扫描
            scanRegionForPointers(region->startAddress, region->endAddress);
        }
    } else {
        //线程池扫描
        scanRegionForPointers(startAddress, endAddress);
    }
    //清楚map 减少内存占用
    addressMap_.clear();
    // 排序指针，便于后续二分查找
    std::sort(pointerCache_.begin(), pointerCache_.end(), 
              [](PointerAllData* a, PointerAllData* b) {
                // 按指针值排序
                  return a->value < b->value;
              });
        std::cout << "扫描潜在指针完成"
              << " 指针数量: " << pointerCache_.size()
              << " 内存使用: " << pointerCache_.size() * sizeof(PointerData) / 1024 / 1024 << " MB"
              << std::endl;
    return static_cast<uint32_t>(pointerCache_.size());
}

void PointerScanner::scanRegionForPointers(Address startAddress, Address endAddress) {

    // 获取页面大小
    const size_t PAGE_SIZE = sysconf(_SC_PAGESIZE);
    const size_t BUFFER_SIZE = PAGE_SIZE;
    
    std::vector<uint8_t> buffer(BUFFER_SIZE);
    std::error_code ec;
    // 清除地址映射
    addressMap_.clear();
    for (Address addr = startAddress; addr < endAddress; addr += BUFFER_SIZE) {
        size_t readSize = std::min<size_t>(BUFFER_SIZE, endAddress - addr);
        
        // 读取内存块
        if (!memoryAccess_->read(addr, buffer.data(), readSize, ec)) {
            continue;  // 如果读取失败，跳过此块
        }
        
        // 扫描可能的指针 (64位系统)
        for (size_t i = 0; i < readSize; i += sizeof(Address)) {
            if (i + sizeof(Address) > readSize) {
                break;
            }
            
            // 读取潜在的指针值
            Address value = *reinterpret_cast<Address*>(buffer.data() + i);

            if (!isValidAddress(value))
            {
                continue;
            }
            
            // 
            //if (memoryAccess_->isValidAddress(value)) 
            {
                Address pointerAddr = addr + i;
                //printf("pointerAddr: %lx -> %lx\n", pointerAddr, value);
                // 避免重复
                if (addressMap_.find(pointerAddr) == addressMap_.end()) {
                    auto* pointerallData = new PointerAllData(
                                                pointerAddr, 
                                            value,startAddress,0,
                                            calculateStaticOffset(pointerAddr));
                    pointerCache_.push_back(pointerallData);
                    addressMap_[pointerAddr] = new PointerData(pointerAddr, value);
                    //TODO 保存到临时文件
                    //扫描潜在指针完成 指针数量: 19711086 内存使用: 451 MB
                    //试了一个大型游戏 ，扫一遍也就占用几百mb ，直接内存
                    //后续构建树节点 内存炸了 将节点保存到文件
                }
            }
        }//for readSize
    }//for startAddress

}



void PointerScanner::Search1Pointers(
    std::vector<std::vector<PointerRange>>& dirs,
    std::vector<PointerRange>& ranges,
    PointerData* pointers,
    const ScanOptions& options
    ) {
        PointerDir* firstPointerDir = new PointerDir(pointers->address, pointers->address, nullptr, nullptr);
        // 二分查找在内存区域中的指针
        //第一层是找到地址进行匹配
        auto startIt = std::lower_bound(pointerCache_.begin(), pointerCache_.end(), pointers->address,
                                      [&](PointerAllData* p, Address addr) {
                                          return p->value < (addr - options.maxOffset);
                                      });
        auto endIt = std::upper_bound(pointerCache_.begin(), pointerCache_.end(), pointers->address,
                                    [&](Address addr, PointerAllData* p) {
                                        return addr < p->value;
                                    });
        
        // 如果区域中有指针
        if ( startIt != pointerCache_.end()
         && endIt != pointerCache_.end()
         && startIt <= endIt) {
            std::vector<PointerDir> regionResults; 
            // 添加到已处理列表
            for (auto it = startIt; it != endIt; ++it) {
                // 为每个指针创建PointerDir并初始化新字段
                Offset offset = static_cast<Offset>((*it)->value - pointers->address);
  
                PointerDir dir((*it)->value, (*it)->address,offset, (*it)->staticOffset_);
                dir.chainId = 0;  // 初始chainId为0
                dir.parent = nullptr;
                dir.child = firstPointerDir;
                //firstPointerDir->parent = &dir;
                regionResults.push_back(std::move(dir));
            }            
            std::cout << "第一层指针数量: " << regionResults.size() << std::endl;
            // 创建指针范围
            ranges.emplace_back(0, pointers->address, std::move(regionResults));
        } else {
            std::cout << "没有找到第一层指针 程序退出" << std::endl;
            exit(0);
        }
      
        dirs[0] = ranges;
}

// 判断地址是否在静态区域内
StaticOffset PointerScanner::calculateStaticOffset(Address addr) {
    // 遍历所有静态区域
    for ( auto* region : staticRegionList) {
        // 检查地址是否落在这个区域内
        if (addr >= region->startAddress && addr < region->endAddress) {
            return StaticOffset(addr - region->startAddress, region);
        }
    }
     
    //二分查找
    // auto it = std::lower_bound(staticRegionList.begin(), staticRegionList.end(), addr,
    //                           [](const MemoryRegion* region, Address addr) {
    //                               return region->startAddress < addr;
    //                           });
    // if(it != staticRegionList.end())
    // {
    //     return StaticOffset(addr - (*it)->startAddress, *it);
    // }
    return StaticOffset(0, nullptr);
}

std::shared_ptr<PointerChain> PointerScanner::scanPointerChain(
    Address& targetAddress,
    const ScanOptions& options,
    const ProgressCallback& progressCb) {

    // 准备数据结构
    std::vector<std::vector<PointerRange>> dirs(options.maxDepth + 1);
    std::vector<PointerRange> ranges;
    
    auto startTime = std::chrono::high_resolution_clock::now();
    std::cout << "开始扫描指针链..." << std::endl;
    
    // 第一级：目标地址
    auto* data = new PointerData(targetAddress, 0);

    // 处理第0层（目标地址） 
    Search1Pointers(dirs, ranges, data, options);

    // 开始逐层扫描
    for (size_t level = 1; level <= options.maxDepth; ++level)
    {
        // 获取当前层的指针数量
        size_t prevLevelDirCount = dirs[level - 1].size();
        if (prevLevelDirCount == 0)
        {
            break; // 如果上一层没有指针，则终止
        }

        std::cout << "扫描第 " << level << " 层指针..." << std::endl;

        // 创建当前层的结果容器
        dirs[level].clear();
        // 遍历dirs[level - 1]中的指针 进行扫描内存 获取新一轮的指针
        for (auto& pointerRange : dirs[level - 1])
        {
            for (auto& p : pointerRange.results)
            {
                ranges.clear(); 
                if (p.staticOffset_.staticOffset > 0)
                {
                    continue;
                }
                // 开始二分查找指针内存 进行扫描
                auto startIt = std::lower_bound(pointerCache_.begin(), pointerCache_.end(), p.address,
                                                [&](PointerAllData *p, Address addr)
                                                {
                                                    return p->value < (addr - options.maxOffset);
                                                });
                auto endIt = std::upper_bound(pointerCache_.begin(), pointerCache_.end(), p.address,
                                              [&](Address addr, PointerAllData *p)
                                              {
                                                  return addr  < p->value;
                                              });
                if (startIt <= endIt && startIt != pointerCache_.end())
                {
                  
                    // 如果区域中有指针
                    std::vector<PointerDir> regionResults;
                    regionResults.reserve(std::distance(startIt, endIt));
                    // 遍历找到的所有可能指针
                    for (auto it = startIt; it != endIt; ++it)
                    {
                        
                        PointerAllData *potentialPointer = *it;

                        // 计算偏移量
                        Offset offset = potentialPointer->value - p.address;
                        
                        // 创建新的PointerDir并初始化字段
                        PointerDir dir(potentialPointer->value,
                                       potentialPointer->address,
                                       offset,
                                       potentialPointer->staticOffset_);
                        dir.chainId = 0;  // 初始chainId为0
                        dir.parent = nullptr;
                        dir.child = &p;
                        
                        regionResults.push_back(std::move(dir));
                    } // for
                  
                    if (!regionResults.empty()) {
                        ranges.emplace_back(level, p.address, std::move(regionResults));
                    }
                } // 二分数据

            } // for p

            // 加入上层数据
            if (!ranges.empty()) {
                dirs[level].insert(dirs[level].end(), ranges.begin(), ranges.end());
            }

        } // for dir[level]

        // 报告进度
        if (progressCb) {
            progressCb(level, options.maxDepth, static_cast<float>(level) / options.maxDepth);
        }
    } // for level

    // 构建指针树
    auto chainInfo = buildPointerDirsTree(dirs);
    
    // 打印所有有效的指针链
    printPointerChains(chainInfo);
    
    // 创建最终的指针链结果
    auto result = std::make_shared<PointerChain>();
    
    // 添加所有层级
    size_t maxLevel = options.maxDepth < dirs.size() - 1 ? options.maxDepth : dirs.size() - 1;
    for (size_t level = 0; level <= maxLevel; ++level) {
        result->addLevel(level);
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    std::cout << "指针链扫描完成，耗时: " << duration << "ms" << std::endl;
    
    std::cout << "找到 " << (result->isEmpty() ? 0 : result->getChainCount()) << " 个可能的指针链" << std::endl;
    
    delete data;
    
    return result;
}




ChainInfo<size_t> PointerScanner::buildPointerDirsTree(
    std::vector<std::vector<PointerRange>>& dirs
) {
    // 初始化结果
    ChainInfo<size_t> result;
    
    // 层级数量
    size_t levelCount = dirs.size();
    if (levelCount == 0) {
        return result;
    }
    
    // 初始化计数数组和内容数组
    result.counts.resize(levelCount);
    result.contents.resize(levelCount);
    
    // 找出目标地址
    Address targetAddress = 0;
    if (!dirs[0].empty() && !dirs[0][0].results.empty()) {
        targetAddress = dirs[0][0].address;
    } else {
        std::cout << "没有找到目标地址" << std::endl;
        return result;
    }
    
    std::cout << "目标地址: 0x" << std::hex << targetAddress << std::dec << std::endl;
    
    // 收集每层的所有指针引用
    std::vector<std::vector<PointerDir*>> levelPointers(levelCount);
    
    // 收集所有层级的指针
    for (size_t level = 0; level < levelCount; ++level) {
        for (auto& range : dirs[level]) {
            for (auto& dir : range.results) {
                // 将所有指针都添加到对应层
                levelPointers[level].push_back(&dir);
            }
        }
        
        // 为结果数组创建空间
        if (!levelPointers[level].empty()) {
            result.counts[level].resize(levelPointers[level].size() + 1, 0);
            result.contents[level] = levelPointers[level];
        }
    }
    
    // 找出所有的静态指针
    std::vector<std::pair<size_t, PointerDir*>> staticPointers;
    for (size_t level = 0; level < levelCount; ++level) {
        for (auto* dir : levelPointers[level]) {
            if (dir->staticOffset_.staticOffset > 0) {
                staticPointers.emplace_back(level, dir);
            }
        }
    }
    
    std::cout << "找到 " << staticPointers.size() << " 个静态指针作为链的起点" << std::endl;
    
    if (staticPointers.empty()) {
        std::cout << "没有找到静态指针作为链的起点，无法构建有效指针链" << std::endl;
        return result;
    }
    
    // 从静态指针开始构建链
    size_t chainCount = 0;
    std::vector<SimplePointerChain> chains;
    
    for (auto& [level, staticPtr] : staticPointers) {
        chainCount++;
        staticPtr->chainId = chainCount;
        
        // 创建新的指针链
        SimplePointerChain chain;
        chain.nodes.push_back(staticPtr);
        chain.baseAddress = staticPtr->address;
        chain.baseRegion = staticPtr->staticOffset_.region;
        chain.staticOffset = staticPtr->staticOffset_.staticOffset;
        
        // 从静态指针开始，尝试向下追溯到目标地址
        bool foundPath = false;
        PointerDir* currentPtr = staticPtr;
        
        // 从当前层向下查找
        for (int currLevel = level; currLevel > 0; --currLevel) {
            bool foundNext = false;
            
            // 在下一层查找被当前指针引用的指针
            for (auto* nextPtr : levelPointers[currLevel - 1]) {
                // 如果当前指针的值等于下一层指针的地址+偏移，则它们连接
                if (currentPtr->value == nextPtr->address + nextPtr->offset) {
                    // 建立父子关系
                    currentPtr->child = nextPtr;
                    nextPtr->parent = currentPtr;
                    nextPtr->chainId = chainCount;
                    
                    // 更新当前指针
                    currentPtr = nextPtr;
                    chain.nodes.push_back(currentPtr);
                    foundNext = true;
                    
                    // 如果到达了最后一层，检查是否是目标地址
                    if (currLevel == 1 && (currentPtr->value == targetAddress)) {
                        foundPath = true;
                        chain.targetAddress = targetAddress;
                    }
                    
                    break; // 找到了下一个节点，停止搜索
                }
            }
            
            if (!foundNext) {
                break; // 找不到下一个节点，终止搜索
            }
        }
        
        // 如果找到了从静态指针到目标地址的完整路径，保存这条链
        if (foundPath) {
            chains.push_back(std::move(chain));
            
            // 更新每个节点的计数
            for (auto* node : chains.back().nodes) {
                size_t nodeIndex = 0;
                size_t nodeLevel = 0;
                
                // 找到节点在levelPointers中的索引和层级
                for (size_t l = 0; l < levelCount; ++l) {
                    for (size_t i = 0; i < levelPointers[l].size(); ++i) {
                        if (levelPointers[l][i] == node) {
                            nodeLevel = l;
                            nodeIndex = i;
                            // 更新计数
                            result.counts[l][i + 1] = chains.size();
                            break;
                        }
                    }
                }
            }
        }
    }
    
    // 打印链数统计
    std::cout << "链数统计:" << std::endl;
    std::cout << "  总共找到 " << chains.size() << " 条有效的指针链" << std::endl;
    
    for (size_t level = 0; level < levelCount; ++level) {
        if (!result.contents[level].empty()) {
            size_t validNodes = 0;
            for (size_t i = 0; i < result.contents[level].size(); ++i) {
                if (result.counts[level][i + 1] > result.counts[level][i]) {
                    validNodes++;
                }
            }
            
            if (validNodes > 0) {
                std::cout << "  层级 " << level << ": " << validNodes << " 个节点参与链条" << std::endl;
            }
        }
    }
    
    return result;
}



// 检查地址是否合法的辅助函数
bool PointerScanner::isValidAddress(Address& addr) {
    if ((addr & 0xffff000000000000) == 0xb400000000000000) {
        addr &= 0xffffffffffff;
    }
    addr = addr & 0xFFFFFFFFFFFF;
    // 过滤掉一些显然无效的地址范围
    if (addr < 0x4500000000) return false; // 过滤NULL和极小值
    if (addr > 0x7FFFFFFFFF) return false; // 过滤超大值

    // 在Android系统上，有效地址通常是某些范围
    // 0x70000000 - 0x80000000 通常是系统库
    // 0x12000000 - 0x14000000 通常是应用程序堆
    
    // 检查地址是否对齐（通常指针是4或8字节对齐的）
    if (addr % 4 != 0) return false;
    
    return true;
}

void PointerScanner::printPointerChains(const ChainInfo<size_t>& chainInfo) {
    if (chainInfo.contents.empty() || chainInfo.counts.empty()) {
        std::cout << "没有找到有效的指针链" << std::endl;
        return;
    }

    std::cout << "开始打印所有有效的指针链..." << std::endl;
    size_t chainCount = 0;
    
    // 首先构建一个当前有效的链列表
    std::map<size_t, std::vector<PointerDir*>> validChains; // chainId -> 节点列表
    
    // 找出所有带有chainId的节点
    for (size_t level = 0; level < chainInfo.contents.size(); ++level) {
        for (size_t i = 0; i < chainInfo.contents[level].size(); ++i) {
            PointerDir* node = chainInfo.contents[level][i];
            if (node->chainId > 0) {
                validChains[node->chainId].push_back(node);
            }
        }
    }
    
    // 按照chainId排序打印每条链
    for (auto& [id, nodes] : validChains) {
        // 找出静态指针（链的起点）
        PointerDir* startNode = nullptr;
        for (auto* node : nodes) {
            if (node->staticOffset_.staticOffset > 0 && node->parent == nullptr) {
                startNode = node;
                break;
            }
        }
        
        if (!startNode) continue;
        
        // 从静态指针开始，根据parent/child关系重建完整链
        std::vector<PointerDir*> chainPath;
        PointerDir* currentNode = startNode;
        
        // 添加起点
        chainPath.push_back(currentNode);
        
        // 跟踪子指针直到末尾
        while (currentNode->child != nullptr) {
            currentNode = currentNode->child;
            chainPath.push_back(currentNode);
        }
        
        // 确认这是一条有效的链（至少有起点和终点）
        if (chainPath.size() < 2) continue;
        
        // 打印链信息
        chainCount++;
        std::cout << "链 #" << chainCount << ":" << std::endl;
        
        // 打印链的起点(静态指针)
        PointerDir* staticPtr = chainPath[0];
        std::cout << "  静态基址: 0x" << std::hex << staticPtr->address;
        std::cout << " (偏移: 0x" << staticPtr->staticOffset_.staticOffset;
        if (staticPtr->staticOffset_.region) {
            std::cout << ", 区域: " << staticPtr->staticOffset_.region->name;
        }
        std::cout << ")" << std::dec << std::endl;
        
        // 打印完整链路
        for (size_t i = 0; i < chainPath.size(); ++i) {
            const PointerDir* ptr = chainPath[i];
            
            std::cout << "  层级 " << i << ": ";
            std::cout << "地址: 0x" << std::hex << ptr->address;
            std::cout << " -> 值: 0x" << ptr->value;
            std::cout  << " 偏移: " << -ptr->offset << std::dec;
            
            if (i < chainPath.size() - 1) {
                // 计算与下一个指针的偏移
                const PointerDir* nextPtr = chainPath[i + 1];
                std::cout << " (到下一级偏移(十进制): ";
                if (ptr->value > nextPtr->address) {
                    std::cout << "+";
                }
                std::cout 
                          << -(static_cast<int64_t>(ptr->value) - static_cast<int64_t>(nextPtr->address)) 
                          << ")" << std::dec;
            } else {
                // 这是目标地址
                std::cout << " (目标地址)";
            }
            
            std::cout << std::endl;
        }
        
        std::cout << std::endl;
    }
    
    std::cout << "总共打印了 " << chainCount << " 条有效的指针链" << std::endl;
}

} // namespace memchainer
