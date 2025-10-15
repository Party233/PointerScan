#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <cstring>
#include <sys/user.h>
#include <functional>

#include "common/types.h"
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
        
        // 读取内存块  todo 批量读取 process_vm_readv支持读取多页
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
                                            value,
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

// 判断地址是否在静态区域内
StaticOffset* PointerScanner::calculateStaticOffset(Address addr) {
    // 遍历所有静态区域
    for ( auto* region : staticRegionList) {
        // 检查地址是否落在这个区域内
        if (addr >= region->startAddress && addr < region->endAddress) {
            return new StaticOffset(addr - region->startAddress, region);
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
    return new StaticOffset(0, nullptr);
}


void PointerScanner::Search1Pointers(
    PointerRange &dirs, std::vector<uint64_t> pointers,
    const ScanOptions &options) {
  auto BaseAddr = pointers[0];
  uint64_t startAddr = BaseAddr - options.maxOffset;
  uint64_t endAddr = BaseAddr;
  printf("第0层查找范围: %lx - %lx\n", (unsigned long)startAddr, (unsigned long)endAddr);
  // 二分查找在内存区域中的指针 pointerCache_是按照value排序的
  // 第一层是找到地址进行匹配  小找小 大找大
  auto startIt = std::lower_bound(pointerCache_.begin(), pointerCache_.end(),
  startAddr,
                                [&](PointerAllData* p, Address addr) {
                                    return p->value < addr ;
                                });
  auto endIt = std::upper_bound(pointerCache_.begin(), pointerCache_.end(),
  endAddr ,
                              [&](Address addr, PointerAllData* p) {
                                  return addr < p->value;
                              });

  PointerRange ranges;
  // 如果区域中有指针
  // startIt == endIt 时可能是：1) 都指向唯一匹配元素（需要处理） 2)
  // 都为end()（不处理）
  if (startIt != pointerCache_.end() && startIt <= endIt && (*startIt)->value <= endAddr) {
    std::vector<PointerDir> regionResults;
    // 添加到已处理列表
    // 使用 <= 确保当 startIt == endIt 时（唯一匹配元素）也能被处理
    for (auto it = startIt; it != pointerCache_.end() && it <= endIt && (*it)->value <= endAddr; ++it) {
      // 为每个指针创建PointerDir并初始化新字段
      Offset offset = static_cast<Offset>(BaseAddr - (*it)->value);
      
      PointerDir dir(*it, offset);
      // 第一层的 父节点 置为空，链在目标地址结束
      dir.child = nullptr;
      regionResults.push_back(std::move(dir));
    }

    printf("第0层指针数量: %zu\n", regionResults.size());
    // 创建指针范围
    //ranges.emplace_back(0, BaseAddr, std::move(regionResults));
    ranges.results = std::move(regionResults);
    ranges.level = 0;
    ranges.address = BaseAddr;
  } else {
    printf("没有找到第0层指针 程序退出");
    // exit(0);
  }

  dirs = ranges;
}

int PointerScanner::scanPointerChain(Address &targetAddress,
                                     const ScanOptions &options,
                                     const ProgressCallback &progressCb) {
  // 清空之前的结果
  chains_.clear();

  // 打印options参数
  printf("options参数: maxDepth: %d, maxOffset: %d, limitResults: %d, "
         "resultLimit: %d\n",
         options.maxDepth, options.maxOffset, options.limitResults,
         options.resultLimit);

  auto startTime = std::chrono::high_resolution_clock::now();
  auto lastProgressTime = startTime;

  // 第0级：目标地址
  std::vector<uint64_t> pointers;
  pointers.push_back(targetAddress);

  printf("....开始扫描第0层指针链...\n");
  // 处理第0层（目标地址）- 找到所有指向目标地址的指针
  PointerRange level0Results;
  Search1Pointers(level0Results, pointers, options);

  if (level0Results.results.empty()) {
    printf("第0层未找到任何指针\n");
    return 0;
  }

  size_t totalLevel0Branches = level0Results.results.size();
  printf("第0层找到 %zu 个指针，开始深度递归扫描...\n", totalLevel0Branches);

  // 统计信息
  size_t totalChainsFound = 0;
  size_t totalNodesProcessed = 0;
  size_t processedLevel0Branches = 0;
  
  // 进度报告控制
  const size_t progressReportInterval = 5000; // 每处理5000个节点报告一次
  size_t nextProgressReport = progressReportInterval;

  // 临时存储找到的所有静态指针链，最后统一保存到 chains_
  std::vector<std::list<PointerChainNode>> tempChains;
  tempChains.reserve(1000); // 预分配空间，减少重新分配

  // 深度优先搜索递归函数
  std::function<void(PointerDir *, int)> dfsSearch =
      [&](PointerDir *currentNode, int currentDepth) {
        // 检查结果数量限制
        if (options.limitResults && totalChainsFound >= options.resultLimit) {
          return;
        }

        // 检查深度限制
        if (currentDepth > options.maxDepth) {
          return;
        }

        // 如果当前节点是静态指针，立即构建完整指针链并暂存
        // 注意：必须在递归栈还有效时立即构建，因为中间节点使用的是栈内存
        if (currentNode->Data->staticOffset_->staticOffset > 0) {
          // 构建完整指针链（从静态地址到目标地址）
          std::list<PointerChainNode> chain;

          // 从当前静态节点开始，沿着child指针向下遍历
          PointerDir *node = currentNode;
          while (node != nullptr) {
            PointerChainNode chainNode(node->Data->address, node->Data->value,
                                       node->offset, node->Data->staticOffset_);
            chain.push_back(chainNode);
            node = node->child;
          }

          // 保存有效的指针链
          tempChains.push_back(std::move(chain));
          totalChainsFound++;

          // 每找到100条链报告一次
          if (totalChainsFound % 100 == 0) {
            printf("已找到 %zu 条有效指针链\n", totalChainsFound);
          }

          return; // 找到静态指针，终止这条路径的搜索
        }

        // 获取当前节点的地址，搜索指向它的指针
        Address baseAddr = currentNode->Data->address;
        Address startAddr = baseAddr - options.maxOffset;
        Address endAddr = baseAddr;

        // 二分查找可能的父指针
        auto startIt = std::lower_bound(
            pointerCache_.begin(), pointerCache_.end(), startAddr,
            [&](PointerAllData *p, Address addr) { return p->value < addr; });
        auto endIt = std::upper_bound(
            pointerCache_.begin(), pointerCache_.end(), endAddr,
            [&](Address addr, PointerAllData *p) { return addr < p->value; });

        if (startIt == pointerCache_.end() || startIt > endIt ||
            (*startIt)->value > endAddr) {
          return; // 没有找到父指针，终止这条路径
        }

        // 计算分支数量并更新统计
        size_t branchCount = std::distance(startIt, endIt);
        totalNodesProcessed += branchCount;

        // 遍历所有可能的父指针，递归搜索
        for (auto it = startIt; it <= endIt && it != pointerCache_.end() && (*it)->value <= endAddr;
             ++it) {
          // 检查是否需要提前终止
          if (options.limitResults && totalChainsFound >= options.resultLimit) {
            break;
          }

          PointerAllData *parentPointer = *it;

          // 计算偏移量
          Offset offset = static_cast<Offset>(baseAddr - parentPointer->value);

          // 创建临时节点（不分配堆内存，使用栈内存）
          PointerDir tempNode(parentPointer, offset);
          tempNode.child = currentNode;

          // 递归搜索下一层
          dfsSearch(&tempNode, currentDepth + 1);
        }

        // 定期报告进度（基于节点处理数量）
        if (progressCb && totalNodesProcessed >= nextProgressReport) {
          auto currentTime = std::chrono::high_resolution_clock::now();
          auto timeSinceLastProgress = std::chrono::duration_cast<std::chrono::milliseconds>(
              currentTime - lastProgressTime).count();
          
          // 至少间隔500ms才报告一次，避免过于频繁
          if (timeSinceLastProgress >= 500) {
            // 计算进度：基于已处理的第0层分支数量
            float branchProgress = static_cast<float>(processedLevel0Branches) / totalLevel0Branches;
            
            // 综合考虑深度和分支进度
            float overallProgress = branchProgress * 0.9f + 
                                   (static_cast<float>(currentDepth) / options.maxDepth) * 0.1f;
            
            progressCb(currentDepth, options.maxDepth, overallProgress);
            
            lastProgressTime = currentTime;
            nextProgressReport = totalNodesProcessed + progressReportInterval;
          }
        }
      };

  // 从第0层的每个指针开始深度优先搜索
  printf("开始遍历 %zu 个第0层分支...\n", totalLevel0Branches);
  
  for (size_t i = 0; i < level0Results.results.size(); ++i) {
    auto &firstLevelPointer = level0Results.results[i];

    // 每处理一定数量的分支，报告一次进度
    if (progressCb && i % 10 == 0) {
      float progress = static_cast<float>(i) / totalLevel0Branches;
      progressCb(1, options.maxDepth, progress);
    }

    // 直接调用dfsSearch，统一由递归函数处理静态指针检查
    dfsSearch(&firstLevelPointer, 1);
    processedLevel0Branches++;

    // 检查是否达到结果限制
    if (options.limitResults && totalChainsFound >= options.resultLimit) {
      printf("已达到结果限制 %d，停止扫描\n", options.resultLimit);
      break;
    }
  }

  auto endTime = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime)
          .count();

  printf("========== 指针链扫描完成 ==========\n");
  printf("找到有效指针链: %zu 条\n", totalChainsFound);
  printf("处理节点总数: %zu\n", totalNodesProcessed);
  printf("扫描耗时: %lld ms\n", duration);

  // 统一保存所有找到的指针链到 chains_
  if (!tempChains.empty()) {
    printf("开始保存 %zu 条指针链到结果集...\n", tempChains.size());

    // 预分配空间，避免多次扩容
    chains_.reserve(chains_.size() + tempChains.size());

    // 移动所有链到最终结果
    for (auto &chain : tempChains) {
      chains_.push_back(std::move(chain));
    }

    printf("指针链保存完成，当前结果集共 %zu 条\n", chains_.size());
  } else {
    printf("未找到任何有效指针链\n");
  }

  return static_cast<int>(chains_.size());
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
