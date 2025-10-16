#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>
#include <iostream>
#include <cstring>
#include <sys/user.h>
#include <functional>
#include <mutex>

#include "common/types.h"
#include "common/thread_pool.h"
#include "scanner/scanner.h"
#include "scanner/formatter.h"

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
  // 清理旧指针
  pointerCache_.clear();

  // 获取过滤的内存区域
  auto regions = memoryMap_->getFilteredRegions();
  
  if (regions.empty()) {
    std::cout << "没有找到可扫描的内存区域\n";
    return 0;
  }

  std::cout << "开始并行扫描 " << regions.size() << " 个内存区域...\n";
  auto startTime = std::chrono::high_resolution_clock::now();

  // 检查全局线程池是否可用
  if (!globalThreadPool) {
    std::cerr << "错误: 全局线程池未初始化，回退到单线程扫描\n";
    // 回退到单线程扫描
    for (const auto *region : regions) {
      scanRegionForPointers(region->startAddress, region->endAddress);
    }
  } else {
    // 使用线程池并行扫描
    std::cout << "使用 " << globalThreadPool->size() << " 个线程并行扫描\n";
    
    // 存储所有任务的 future
    std::vector<std::future<std::vector<PointerAllData*>>> futures;
    futures.reserve(regions.size());

    // 为每个区域提交扫描任务
    for (const auto *region : regions) {
      auto future = globalThreadPool->submit(
        [this, region]() -> std::vector<PointerAllData*> {
          // 使用局部缓存收集该区域的指针，减少锁竞争
          std::vector<PointerAllData*> localCache;
          
          std::vector<uint8_t> buffer(PAGE_SIZE);
          std::error_code ec;
          
          Address startAddr = region->startAddress;
          Address endAddr = region->endAddress;

          for (Address addr = startAddr; addr < endAddr; addr += PAGE_SIZE) {
            size_t readSize = std::min<size_t>(PAGE_SIZE, endAddr - addr);
            
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

              if (!const_cast<PointerScanner*>(this)->isValidAddress(value)) {
                continue;
              }
              
              Address pointerAddr = addr + i;
              auto* pointerAllData = new PointerAllData(
                pointerAddr, 
                value,
                calculateStaticOffset(pointerAddr)
              );
              localCache.push_back(pointerAllData);
            }
          }
          
          return localCache;
        }
      );
      
      futures.push_back(std::move(future));
    }

    // 收集所有线程的结果
    std::cout << "等待所有扫描任务完成...\n";
    size_t completedRegions = 0;
    
    for (auto& future : futures) {
      try {
        // 获取任务结果
        auto localCache = future.get();
        
        // 批量添加到全局缓存（加锁保护）
        {
          std::lock_guard<std::mutex> lock(pointerCacheMutex_);
          pointerCache_.insert(pointerCache_.end(), 
                              localCache.begin(), 
                              localCache.end());
        }
        
        completedRegions++;
        if (completedRegions % 10 == 0 || completedRegions == regions.size()) {
          std::cout << "进度: " << completedRegions << "/" << regions.size() 
                    << " 个区域已完成\n";
        }
      } catch (const std::exception& e) {
        std::cerr << "扫描任务异常: " << e.what() << std::endl;
      }
    }
  }

  auto scanEndTime = std::chrono::high_resolution_clock::now();
  auto scanDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
    scanEndTime - startTime).count();
  
  std::cout << "扫描完成，耗时: " << scanDuration << " ms\n";
  std::cout << "开始排序指针...\n";

  // 排序指针，便于后续二分查找
  std::sort(pointerCache_.begin(), pointerCache_.end(),
            [](PointerAllData *a, PointerAllData *b) {
              return a->value < b->value;
            });
  
  auto endTime = std::chrono::high_resolution_clock::now();
  auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
    endTime - startTime).count();

  std::cout << "========== 扫描统计 ==========\n"
            << "指针数量: " << pointerCache_.size() << "\n"
            << "内存使用: " << (pointerCache_.size() * sizeof(PointerAllData) / 1024 / 1024) << " MB\n"
            << "扫描耗时: " << scanDuration << " ms\n"
            << "排序耗时: " << (totalDuration - scanDuration) << " ms\n"
            << "总耗时: " << totalDuration << " ms\n"
            << "==============================" << std::endl;
  
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
  static  StaticOffset nullStaticOffset = StaticOffset(0, nullptr);
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
    return &nullStaticOffset;
}


void PointerScanner::Search1Pointers(
    PointerRange &dirs, std::vector<uint64_t> pointers,
    const ScanOptions &options) {
  auto BaseAddr = pointers[0];
  uint64_t startAddr = BaseAddr - options.maxOffset;
  uint64_t endAddr = BaseAddr;
  printf("第0层查找范围: %lx - %lx\n", (unsigned long)startAddr, (unsigned long)endAddr);
  
  // 使用哈希索引查找（O(1) 查找性能）
  auto foundPointers = findPointersInRange(startAddr, endAddr);

  PointerRange ranges;
  
  if (!foundPointers.empty()) {
    std::vector<PointerDir> regionResults;
    regionResults.reserve(foundPointers.size());
    
    // 处理找到的所有指针
    for (auto* pointerData : foundPointers) {
      // 为每个指针创建PointerDir并初始化新字段
      Offset offset = static_cast<Offset>(BaseAddr - pointerData->value);
      
      PointerDir dir(pointerData, offset);
      // 第一层的父节点置为空，链在目标地址结束
      dir.child = nullptr;
      regionResults.push_back(std::move(dir));
    }

    printf("第0层指针数量: %zu\n", regionResults.size());
    // 创建指针范围
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
                                     const std::string& outputFile) {
  // 清空之前的结果
  chains_.clear();

  // 打印options参数
  printf("options参数: maxDepth: %d, maxOffset: %d, limitResults: %d, "
         "resultLimit: %d\n",
         options.maxDepth, options.maxOffset, options.limitResults,
         options.resultLimit);

  auto startTime = std::chrono::high_resolution_clock::now();

  // 初始化输出文件和格式化器（如果指定）
  bool enableStreamOutput = !outputFile.empty();
  PointerFormatter formatter;  // 统一构造一次，避免重复创建
  
  if (enableStreamOutput) {
    if (!formatter.initOutputFile(outputFile)) {
      printf("警告: 无法初始化输出文件 %s，将在扫描结束后统一输出\n", outputFile.c_str());
      enableStreamOutput = false;
    } else {
      printf("边扫边输出模式已启用，结果将实时写入: %s\n", outputFile.c_str());
    }
  }

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

  // 统计信息（使用原子变量支持多线程）
  std::atomic<size_t> totalChainsFound{0};
  std::atomic<size_t> totalNodesProcessed{0};
  std::atomic<size_t> processedLevel0Branches{0};
  
  // 批量写入缓冲区
  std::vector<std::list<PointerChainNode>> writeBuffer;
  const size_t WRITE_BUFFER_SIZE = 500;  // 累积500条链后批量写入
  std::mutex bufferMutex;  // 保护缓冲区的互斥锁
  
  // 结果限制标志（原子操作）
  std::atomic<bool> resultLimitReached{false};
  
  // 批量写入函数（线程安全）
  auto flushWriteBuffer = [&]() {
    if (writeBuffer.empty()) {
      return;
    }
    
    if (!formatter.appendChainsToFile(writeBuffer, outputFile)) {
      printf("警告: 批量写入文件失败\n");
    }
    writeBuffer.clear();
  };

  // 检查是否有全局线程池可用
  bool useMultiThreading = (globalThreadPool != nullptr);
  
  if (useMultiThreading) {
    printf("使用多线程模式，线程数: %zu\n", globalThreadPool->size());
  } else {
    printf("使用单线程模式\n");
  }

  // 深度优先搜索递归函数（线程安全版本）
  std::function<void(PointerDir *, int)> dfsSearch =
      [&](PointerDir *currentNode, int currentDepth) {
        // 检查结果数量限制（原子读取）
        if (options.limitResults && resultLimitReached.load(std::memory_order_relaxed)) {
          return;
        }

        // 检查深度限制
        if (currentDepth > options.maxDepth) {
          return;
        }

        // 如果当前节点是静态指针，立即构建完整指针链
        if (currentNode->Data->staticOffset_->staticOffset > 0) {
          // 先检查是否已达到限制，避免构建不必要的链
          if (options.limitResults && resultLimitReached.load(std::memory_order_relaxed)) {
            return;
          }
          
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

          // 边扫边输出：添加到批量写入缓冲区
          if (enableStreamOutput) {
            std::lock_guard<std::mutex> lock(bufferMutex);
            writeBuffer.push_back(chain);
            
            // 当缓冲区达到指定大小时，批量写入
            if (writeBuffer.size() >= WRITE_BUFFER_SIZE) {
              flushWriteBuffer();
            }
          }
          
          // 更新计数（原子操作）
          size_t currentCount = totalChainsFound.fetch_add(1, std::memory_order_relaxed) + 1;
          
          // 检查是否达到限制
          if (options.limitResults && currentCount >= options.resultLimit) {
            resultLimitReached.store(true, std::memory_order_relaxed);
          }

          // 每找到100条链报告一次
          if (currentCount % 100 == 0) {
            printf("已找到 %zu 条有效指针链\n", currentCount);
          }

          return; // 找到静态指针，终止这条路径的搜索
        }

        // 获取当前节点的地址，搜索指向它的指针
        Address baseAddr = currentNode->Data->address;
        Address startAddr = baseAddr - options.maxOffset;
        Address endAddr = baseAddr;

        // 使用二分查找可能的父指针
        auto parentPointers = findPointersInRange(startAddr, endAddr);

        if (parentPointers.empty()) {
          return; // 没有找到父指针，终止这条路径
        }

        // 更新统计（原子操作）
        totalNodesProcessed.fetch_add(parentPointers.size(), std::memory_order_relaxed);

        // 遍历所有可能的父指针，递归搜索
        for (auto* parentPointer : parentPointers) {
          // 检查是否需要提前终止
          if (options.limitResults && resultLimitReached.load(std::memory_order_relaxed)) {
            break;
          }

          // 计算偏移量
          Offset offset = static_cast<Offset>(baseAddr - parentPointer->value);

          // 创建临时节点（不分配堆内存，使用栈内存）
          PointerDir tempNode(parentPointer, offset);
          tempNode.child = currentNode;

          // 递归搜索下一层
          dfsSearch(&tempNode, currentDepth + 1);
        }
      };

  // 从第0层的每个指针开始深度优先搜索
  printf("开始遍历 %zu 个第0层分支...\n", totalLevel0Branches);
  
  if (useMultiThreading) {
    // ============ 多线程模式 ============
    std::vector<std::future<void>> futures;
    futures.reserve(level0Results.results.size());
    
    // 进度报告互斥锁
    std::mutex progressMutex;
    
    // 为每个第0层分支提交任务到线程池
    for (size_t i = 0; i < level0Results.results.size(); ++i) {
      // 检查是否达到结果限制
      if (options.limitResults && resultLimitReached.load(std::memory_order_relaxed)) {
        break;
      }
      
      auto future = globalThreadPool->submit(
        [&, i, branchIndex = i]() {
          // 获取第0层指针的副本（避免并发访问问题）
          PointerDir firstLevelPointer = level0Results.results[branchIndex];
          
          // 执行DFS搜索
          dfsSearch(&firstLevelPointer, 1);
          
          // 更新进度
          size_t processed = processedLevel0Branches.fetch_add(1, std::memory_order_relaxed) + 1;
          
          // 定期报告进度（减少输出频率）
          if (processed % 100 == 0 || processed == totalLevel0Branches) {
            std::lock_guard<std::mutex> lock(progressMutex);
            float progress = static_cast<float>(processed) / totalLevel0Branches;
            printf("进度: 已处理 %zu/%zu 个分支 (%.1f%%), 找到 %zu 条指针链\n", 
                   processed, totalLevel0Branches, progress * 100.0f, 
                   totalChainsFound.load(std::memory_order_relaxed));
          }
        }
      );
      
      futures.push_back(std::move(future));
    }
    
    // 等待所有任务完成
    printf("等待所有搜索任务完成...\n");
    for (auto& future : futures) {
      try {
        future.get();
      } catch (const std::exception& e) {
        std::cerr << "搜索任务异常: " << e.what() << std::endl;
      }
    }
    
  } else {
    // ============ 单线程模式 ============
    for (size_t i = 0; i < level0Results.results.size(); ++i) {
      auto &firstLevelPointer = level0Results.results[i];

      // 每处理一定数量的分支，报告一次进度
      if (i > 0 && i % 100 == 0) {
        float progress = static_cast<float>(i) / totalLevel0Branches;
        printf("进度: 已处理 %zu/%zu 个分支 (%.1f%%), 找到 %zu 条指针链\n", 
               i, totalLevel0Branches, progress * 100.0f, 
               totalChainsFound.load(std::memory_order_relaxed));
      }

      // 直接调用dfsSearch
      dfsSearch(&firstLevelPointer, 1);
      processedLevel0Branches.fetch_add(1, std::memory_order_relaxed);

      // 检查是否达到结果限制
      if (options.limitResults && resultLimitReached.load(std::memory_order_relaxed)) {
        printf("已达到结果限制 %d，停止扫描\n", options.resultLimit);
        break;
      }
    }
  }

  auto endTime = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime)
          .count();

  // 刷新缓冲区，写入剩余的指针链
  if (enableStreamOutput) {
    std::lock_guard<std::mutex> lock(bufferMutex);
    if (!writeBuffer.empty()) {
      printf("正在写入剩余的 %zu 条指针链...\n", writeBuffer.size());
      flushWriteBuffer();
    }
  }

  // 获取最终统计值
  size_t finalChainCount = totalChainsFound.load(std::memory_order_relaxed);
  size_t finalNodeCount = totalNodesProcessed.load(std::memory_order_relaxed);

  printf("========== 指针链扫描完成 ==========\n");
  printf("找到有效指针链: %zu 条\n", finalChainCount);
  printf("处理节点总数: %zu\n", finalNodeCount);
  printf("扫描耗时: %lld ms\n", duration);

  if (enableStreamOutput) {
    printf("结果已批量写入文件: %s\n", outputFile.c_str());
    printf("批量写入策略: 每 %zu 条链写入一次\n", WRITE_BUFFER_SIZE);
  } else if (finalChainCount == 0) {
    printf("未找到任何有效指针链\n");
  }

  return static_cast<int>(finalChainCount);
}

// 使用二分查找在排序的 pointerCache_ 中查找指向指定地址范围的所有指针
std::vector<PointerAllData*> PointerScanner::findPointersInRange(Address startAddr, Address endAddr) const {
    std::vector<PointerAllData*> result;
    
    // 二分查找范围的起始位置
    auto startIt = std::lower_bound(pointerCache_.begin(), pointerCache_.end(), startAddr,
                                    [](PointerAllData* p, Address addr) {
                                        return p->value < addr;
                                    });
    
    // 二分查找范围的结束位置
    auto endIt = std::upper_bound(pointerCache_.begin(), pointerCache_.end(), endAddr,
                                  [](Address addr, PointerAllData* p) {
                                      return addr < p->value;
                                  });
    
    // 收集范围内的所有指针
    if (startIt != pointerCache_.end() && startIt <= endIt && (*startIt)->value <= endAddr) {
        for (auto it = startIt; it != pointerCache_.end() && it <= endIt && (*it)->value <= endAddr; ++it) {
            result.push_back(*it);
        }
    }
    
    return result;
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
