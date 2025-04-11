#pragma once

#include "common/types.h"
#include <vector>
#include <memory>
#include <map>
#include <list>

namespace memchainer {

// 内存映射管理
class MemoryMap {
public:
    MemoryMap();
    ~MemoryMap();

    // 获取进程内存映射
    bool loadMemoryMap(ProcessId pid);
    
    // 设置内存区域过滤器
    void setRegionFilter(int regionTypes);
    
    // 获取所有匹配过滤器的内存区域
    std::vector<MemoryRegion*> getFilteredRegions() ;
    
    // 手动添加内存区域
    MemoryRegion* addCustomRegion(Address start, Address end, const char* name, bool filterable = false);
    
    // 清除所有内存区域
    void clear();

    // 获取区域数量
    size_t getRegionCount() const;
    
    // 获取当前过滤器
    int getRegionFilter() const;
    
    // 添加parseProcessModule方法
    bool parseProcessModule();
    
    // 打印内存区域信息
    void printRegionInfo(std::vector<MemoryRegion*> memoryRegions_);

    // 应用智能内存区域过滤
    void applySmartFilter();

private:
    // 解析/proc/pid/maps文件
    bool parseProcessMaps(ProcessId pid);
    
    // 确定内存区域类型
    int determineRegionType(const std::string& name, const std::string& permissions);

    // 获取权限保护标志
    int getPermissionProtFlags(const std::string& permissions);

    std::list<MemoryRegion*> memoryRegions_;
    int regionFilter_;
    ProcessId currentPid_;

    // 区域索引加速查询
    struct RegionIndex {
        std::map<Address, MemoryRegion*> addressToRegion;
        
        void rebuild(const std::list<MemoryRegion*>& regions) {
            addressToRegion.clear();
            for (auto* region : regions) {
                addressToRegion[region->startAddress] = region;
            }
        }
        
        MemoryRegion* findRegion(Address addr) const {
            if (addressToRegion.empty()) return nullptr;
            
            auto it = addressToRegion.upper_bound(addr);
            if (it == addressToRegion.begin()) return nullptr;
            
            --it;
            MemoryRegion* region = it->second;
            
            if (addr >= region->startAddress && addr < region->endAddress) {
                return region;
            }
            
            return nullptr;
        }
    };
    
    RegionIndex regionIndex_;
};

} // namespace memchainer
