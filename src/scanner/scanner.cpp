#include <algorithm>
#include <chrono>
#include <iostream>
#include <fstream>
#include <cstring>
#include <sys/user.h>
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
#include <sys/sysconf.h>

namespace memchainer {

PointerScanner::PointerScanner() {
    // 创建文件缓存
    //fileCache_ = std::make_shared<FileCache>();
    //fileCache_->initialize();
}

PointerScanner::~PointerScanner() {
    // 清理指针缓存
    for (auto* ptr : pointerCache_) {
        delete ptr;
    }
    pointerCache_.clear();
    
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

uint32_t PointerScanner::findPointers() {

  // // 清理旧指针
  // for (auto* ptr : pointerCache_) {
  //     delete ptr;
  // }
  pointerCache_.clear();
 

  // 获取过滤的内存区域
  auto regions = memoryMap_->getFilteredRegions();

  // 扫描所有过滤的区域
  for (const auto *region : regions) {
    // TODO 线程池扫描
    scanRegionForPointers(region->startAddress, region->endAddress);
  }

  // 排序指针，便于后续二分查找
  std::sort(pointerCache_.begin(), pointerCache_.end(),
            [](PointerAllData *a, PointerAllData *b) {
              // 按指针值排序
              return a->value < b->value;
            });
  std::cout << "扫描潜在指针完成\n"
            << " 指针数量: " << pointerCache_.size() << " 内存使用: "
            << pointerCache_.size() * sizeof(PointerAllData) / 1024 / 1024
            << " MB" << std::endl;
  return static_cast<uint32_t>(pointerCache_.size());
}

void PointerScanner::scanRegionForPointers(Address startAddress, Address endAddress) {


    std::vector<uint8_t> buffer(PAGE_SIZE);
    std::error_code ec;

    for (Address addr = startAddress; addr < endAddress; addr += PAGE_SIZE) {
        size_t readSize = std::min<size_t>(PAGE_SIZE, endAddress - addr);
        
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
            
            {
                Address pointerAddr = addr + i;
                auto* pointerallData = new PointerAllData(
                                                pointerAddr, 
                                            value,startAddress,0,
                                            calculateStaticOffset(pointerAddr));
                pointerCache_.push_back(pointerallData);
                   
                    //TODO 保存到临时文件
                    //扫描潜在指针完成 指针数量: 19711086 内存使用: 451 MB
                    //试了一个大型游戏 ，扫一遍也就占用几百mb ，直接内存
                    //后续构建树节点 内存炸了 将节点保存到文件
                
            }
        }//for readSize
    }//for startAddress

}




void PointerScanner::Search1Pointers(
    std::vector<std::vector<PointerRange>>& dirs,
    PointerData* pointers,
    const ScanOptions& options
    ) {
        auto BaseAddr = pointers->address;
        // 二分查找在内存区域中的指针 pointerCache_是按照value排序的
        //第一层是找到地址进行匹配  小找小 大找大
        auto startIt = std::lower_bound(pointerCache_.begin(), pointerCache_.end(), BaseAddr- options.maxOffset,
                                      [&](PointerAllData* p, Address addr) {
                                          return p->value <= addr ;
                                      });
        auto endIt = std::upper_bound(pointerCache_.begin(), pointerCache_.end(), BaseAddr ,
                                    [&](Address addr, PointerAllData* p) {
                                        return addr <= p->value;
                                    });
      std::vector<PointerRange> ranges;
        // 如果区域中有指针
        if ( startIt != pointerCache_.end()
         && endIt != pointerCache_.end()
         && startIt <= endIt) {
            std::vector<PointerDir> regionResults; 
            // 添加到已处理列表
            for (auto it = startIt; it != endIt; ++it) {
                // 为每个指针创建PointerDir并初始化新字段
                Offset offset = static_cast<Offset>(pointers->address - (*it)->value);
  
                PointerDir dir((*it)->value, (*it)->address,offset, (*it)->staticOffset_);
                // 第一层的 父节点 置为空，链在目标地址结束
                dir.child = nullptr;
                regionResults.push_back(std::move(dir));
            }            
            std::cout << "第一层指针数量: " << regionResults.size() << std::endl;
            // 创建指针范围
            ranges.emplace_back(0, BaseAddr, std::move(regionResults));
        } else {
            std::cout << "没有找到第一层指针 程序退出" << std::endl;
            exit(0);
        }
      
        dirs[0]=ranges;
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

int PointerScanner::scanPointerChain(
    Address& targetAddress,
    const ScanOptions& options,
    const ProgressCallback& progressCb) {

    // 准备数据结构
    std::vector<std::vector<PointerRange>> dirs(options.maxDepth + 1);
    std::vector<PointerDir> staticPointers;




    
    auto startTime = std::chrono::high_resolution_clock::now();
    std::cout << "开始扫描指针链..." << std::endl;
    
    // 第一级：目标地址
    auto* data = new PointerData(targetAddress, 0);

    // 处理第0层（目标地址） 
    Search1Pointers(dirs, data, options);

    // 开始逐层扫描
    for (size_t level = 1; level <= options.maxDepth; ++level)
    {
        // 获取上层的指针数量
        auto& LastLevelDirsData = dirs[level - 1];
        size_t prevLevelDirCount = LastLevelDirsData.size();
        if (prevLevelDirCount == 0)
        {
            break; // 如果上一层没有指针，则终止
        }

        // 创建当前层的结果容器
        dirs[level].clear();
        // 遍历dirs[level - 1]中的指针 进行扫描内存 获取新一轮的指针
        for (auto& pointerRange : LastLevelDirsData)
        {
            for (auto& p : pointerRange.results)
            {

             
                if (p.staticOffset_.staticOffset > 0)
                {
                    staticPointers.push_back(p);
                    continue;//直接在这里加入到指针链结果中，后面就不要在扫描了
                }
                auto BaseAddr = p.address;
                // 开始二分查找指针内存 进行扫描
                auto startIt = std::lower_bound(pointerCache_.begin(), pointerCache_.end(), BaseAddr- options.maxOffset,
                                                [&](PointerAllData *p, Address addr)
                                                {
                                                    return p->value <= addr;
                                                });
                auto endIt = std::upper_bound(pointerCache_.begin(), pointerCache_.end(), BaseAddr ,
                                              [&](Address addr, PointerAllData *p)
                                              {
                                                  return addr  <= p->value;
                                              });
                //开始构造
                if (startIt <= endIt && startIt != pointerCache_.end())
                {
                  
                    // 如果区域中有指针
                    std::vector<PointerRange> ranges;
                    std::vector<PointerDir> regionResults;
                    regionResults.reserve(std::distance(startIt, endIt));
                    // 遍历找到的所有可能指针
                    for (auto it = startIt; it != endIt; ++it)
                    {
                        
                        PointerAllData *potentialPointer = *it;

                        // 计算偏移量
                        Offset offset = static_cast<Offset>(BaseAddr - potentialPointer->value);
                        
                        // 创建新的PointerDir并初始化字段
                        PointerDir dir(potentialPointer->value,
                                       potentialPointer->address,
                                       offset,
                                       potentialPointer->staticOffset_);
                        //父节点
                        dir.child = &p;
                        
                        regionResults.push_back(std::move(dir));
                    } // for
                  
                    if (!regionResults.empty()) {
                        ranges.emplace_back(level, BaseAddr, std::move(regionResults));
                        // 立刻插入到当前层，避免覆盖其他 p 的结果
                        dirs[level].insert(dirs[level].end(), ranges.begin(), ranges.end());
                        ranges.clear();
                    }
                } // 二分数据

            } // for p

            

        } // for dir[level]

        // 报告进度
        if (progressCb) {
            progressCb(level, options.maxDepth, static_cast<float>(level) / options.maxDepth);
        }
    } // for level

    // // 创建最终的指针链结果
    // auto result = std::make_shared<PointerChain>();
    // result->buildPointerChain(dirs);

    //遍历最后层 加入到指针链结果中
    for(auto& p : dirs[options.maxDepth])
    {
       for(auto& p2 : p.results)
       {
        if(p2.staticOffset_.staticOffset > 0)
        {
            staticPointers.push_back(p2);
        }
       }
    }

    
   if (staticPointers.empty()) {
        std::cout << "没有找到静态指针" << std::endl;
        return 0;
    }
    std::cout << "找到 " << staticPointers.size() << " 个静态指针" << std::endl;
    
    // 从静态指针开始构建指针链
    for ( auto& dir : staticPointers) {
        std::list<PointerChainNode> chain;
        PointerChainNode node(dir.address, dir.value, dir.offset, dir.staticOffset_);
        chain.push_back(node);
        // 开始从顶端遍历子节点构建指针链
        auto dir_ = dir;
        int le =0;
        while (dir_.child != nullptr) {
            PointerChainNode childnode(dir_.child->address,
             dir_.child->value, dir_.child->offset, dir_.child->staticOffset_);
            chain.push_back(childnode);
            dir_ = *dir_.child;
            
           // std::cout << "level: " << le++;
        }
        if(chain.size() > 1){
        chains_.push_back(std::move(chain));
        }
      
        
    }



    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    std::cout << "指针链扫描完成，耗时: "<<std::dec << duration << "ms" << std::endl;
    

    delete data;
    
    return staticPointers.size();
}


// 检查地址是否合法的辅助函数
bool PointerScanner::isValidAddress(Address& addr) {
    if ((addr & 0xffff000000000000) == 0xb400000000000000) {
        addr &= 0xffffffffffff;
    }
   // addr = addr & 0xFFFFFFFFFFFF;
    // 过滤掉一些显然无效的地址范围
    if (addr < 0x4500000000) return false; // 过滤NULL和极小值
    if (addr > 0x7FFFFFFFFF) return false; // 过滤超大值

    // 检查地址是否对齐（通常指针是4或8字节对齐的）
    if (addr % 4 != 0) return false;
    
    return true;
}


} // namespace memchainer
