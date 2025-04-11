#include "memory/mem_map.h"
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <cerrno>
#include <algorithm>
#include <cstdio>
#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>

namespace memchainer {

// 全局变量实例化
std::vector<MemoryRegion*> memoryRegionList;
std::vector<MemoryRegion*> staticRegionList;


bool isFind(std::string name , std::string str){
    return name.find(str)!= std::string::npos ;
}


MemoryMap::MemoryMap() : regionFilter_(MemoryRegionType::All), currentPid_(-1) {
}

MemoryMap::~MemoryMap() {
    clear();
}

bool MemoryMap::loadMemoryMap(ProcessId pid) {
    // 清除旧数据
    clear();
    
    currentPid_ = pid;
    
    // 解析进程内存映射
    return parseProcessMaps(pid);
}

void MemoryMap::printRegionInfo(std::vector<MemoryRegion*> memoryRegions_) {
    std::cout << "内存区域信息:" << std::endl;
    for (const auto& region : memoryRegions_) {
        //if (!region->isFilterable)
        {
            std::cout << "名称: " << region->name 
                  << ", 类型: " << region->type 
                  << ", 起始地址: " << std::hex << region->startAddress 
                  << ", 结束地址: " << region->endAddress << std::dec 
                  << ", 计数: " << region->count 
                  << ", 可过滤: " << region->isFilterable 
                  << std::endl;
        }
        
        
    }
}

void MemoryMap::setRegionFilter(int regionTypes) {
    regionFilter_ = regionTypes;
}

int MemoryMap::getRegionFilter() const {
    return regionFilter_;
}

std::vector<MemoryRegion*> MemoryMap::getFilteredRegions()  {
    std::vector<MemoryRegion*> result;

    applySmartFilter();
    
    for (const auto& region : memoryRegions_) {
        // 如果区域被标记为不可过滤且符合过滤条件
        if (!region->isFilterable && (region->type & regionFilter_)) {
            result.push_back(region);
        }
    }
   
    
    return result;
}

MemoryRegion* MemoryMap::addCustomRegion(Address start, Address end, const char* name, bool filterable) {
    auto* region = new MemoryRegion(start, end, MemoryRegionType::Unknown, name, 0, filterable);
    memoryRegions_.push_back(region);
    staticRegionList.push_back(region); // 添加到全局静态列表
    return region;
}

void MemoryMap::clear() {
    // 清除内存区域
        // 从全局列表中移除
        memoryRegionList.clear();
        staticRegionList.clear();
        for (auto* region : memoryRegions_) 
            delete region;
    memoryRegions_.clear();
}

size_t MemoryMap::getRegionCount() const {
    return memoryRegions_.size();
}

bool MemoryMap::parseProcessMaps(ProcessId pid) {
    char mapsPath[64];
    snprintf(mapsPath, sizeof(mapsPath), "/proc/%d/maps", pid);
    
    std::ifstream mapsFile(mapsPath);
    if (!mapsFile.is_open()) {
        return false;
    }
    
    // 用于跟踪区域名称计数
    std::unordered_map<std::string, int> nameCountMap;
    
    std::string line;
    while (std::getline(mapsFile, line)) {
        // 解析每行内存映射信息
        // 格式: address perms offset dev inode pathname
        
        std::istringstream iss(line);
        std::string addressRange, permissions, offset, dev, inode, pathname;
        
        iss >> addressRange >> permissions >> offset >> dev >> inode;
        
        // 读取路径名（可能包含空格）
        std::getline(iss, pathname);
        // 去除前导空格
        if (!pathname.empty()) {
            auto pos = pathname.find_first_of("/");
            if (pos != std::string::npos)
            {
                pathname = pathname.substr(pos);
                //std::cout << pathname  << "pos  " << pos << std::endl;
            }
            
        }
        
        // 解析地址范围
        size_t dashPos = addressRange.find('-');
        if (dashPos == std::string::npos) {
            continue;
        }
        
        Address startAddr = std::stoull(addressRange.substr(0, dashPos), nullptr, 16);
        Address endAddr = std::stoull(addressRange.substr(dashPos + 1), nullptr, 16);
        
        // 确定区域类型
        int type = determineRegionType(pathname, permissions);
        
        // 确定区域计数
        int count = 0;
        if (!pathname.empty()) {
            count = nameCountMap[pathname]++;
        }
        
        // 创建并添加内存区域
        auto* region = new MemoryRegion(startAddr, endAddr, type, pathname.c_str(), count);
        memoryRegions_.push_back(region);
        memoryRegionList.push_back(region); // 添加到全局列表
    }
    
    return true;
}

bool MemoryMap::parseProcessModule() {
    // 解析进程模块信息，类似于原项目的parse_process_module函数
    if (currentPid_ <= 0) {
        return false;
    }
    
    //printRegionInfo();
    auto  static_type = MemoryRegionType::Code_app | MemoryRegionType::C_data ;

       staticRegionList.clear();
    for (auto* region : memoryRegions_) {
        //处理名字 获取最后一个/后面的部分
        std::string name = region->name;
        auto pos = name.find_last_of("/");
        if (pos!=std::string::npos)
        {
           name = name.substr(pos);
           name = name + "[" + std::to_string(region->count) + "]";
           memset(region->name,0,sizeof(region->name));
           strncpy(region->name,name.c_str(),name.length());
        }
        
        
     
        // 如果找到模块
        if ((region->type & static_type )) {
            staticRegionList.push_back(region);
        }else if (region->type & MemoryRegionType::C_bss)
        {
            auto pre = std::prev(region);
            if ( !(pre->type & static_type))
            {
                continue;
            }
            
            std::string prename = pre->name;
            int pos = prename.find_last_of("/");
            if (pos != std::string::npos)
            {
                prename = prename.substr(pos);
            }
            if (prename.find(".so") != std::string::npos)
            {
                prename = prename + ":bss";
            }
            //std::cout << "   " << prename << std::endl;
            strncpy(region->name,prename.c_str(),prename.length());
            staticRegionList.push_back(region);
        }
        
    }
    //printRegionInfo(staticRegionList);
    return true;
}

int MemoryMap::determineRegionType(const std::string& name, const std::string& permissions) {
    // 根据名称和权限确定内存区域类型，匹配原项目的分类
    
    int type = MemoryRegionType::Unknown;
    
    // 堆
    if (name == "[heap]") {
        type = MemoryRegionType::C_heap;
    }
    // Java堆
    else if ( isFind(name, "dalvik") && isFind(name, "art") ) {
        type = MemoryRegionType::Java_heap;
    }
    // 栈
    else if ( isFind(name, "[stack]") || isFind(name, "[stack:") ) {
        type = MemoryRegionType::Stack;
    }
    // 匿名映射 || isFind(name, "[anon:")
    else if (name.empty()  || 
    name == "[anonymous]") {
        type = MemoryRegionType::Anonymous;
    }
    //libc_malloc 
    else if (isFind(name, "libc_malloc"))
    {
        type = MemoryRegionType::C_alloc;
    }
    //cd
    else if (isFind(name, "/data/app/")  && isFind(permissions, "xp") && isFind(name, ".so"))
    {
        type = MemoryRegionType::Code_app;
    }
    else if (isFind(name, "[anon:.bss]"))
    {
        type = MemoryRegionType::C_bss;
    }
    else if (isFind(name, "/system/framework/"))
    {
        type = MemoryRegionType::Code_system;
    }
    else if (isFind(name, "/data/app/") && isFind(name, ".so"))
    {
        type = MemoryRegionType::C_data;
    }
    // 视频内存
    else if (isFind(name, "/dev/kgsl-3d0")) {
        type = MemoryRegionType::Video;
    }
    // 共享内存
    else if (name.find("ashmem") != std::string::npos) {
        type = MemoryRegionType::Ashmem;
    }
    //bad
    else if (name.find("/system/fonts/") != std::string::npos) {
        type = MemoryRegionType::Bad;
    }
    // 根据权限判断其他类型
    else if (permissions.length() >= 4) {
        // // 如果有执行权限，视为代码段
        // if (permissions[2] == 'xp') {
        //     type = MemoryRegionType::Code_app;
        // }
        // // 可写数据段
        // else if (permissions[1] == 'w') {
        //     type = MemoryRegionType::C_data;
        // }
        // // 只读数据段，可能是BSS
        // else {
        //     type = MemoryRegionType::C_bss;
        // }
        type = MemoryRegionType::Other;
    }
    
    return type;
}

int MemoryMap::getPermissionProtFlags(const std::string& permissions) {
    int prot = PROT_NONE;
    
    if (permissions.length() >= 3) {
        if (permissions[0] == 'r') {
            prot |= PROT_READ;
        }
        if (permissions[1] == 'w') {
            prot |= PROT_WRITE;
        }
        if (permissions[2] == 'x') {
            prot |= PROT_EXEC;
        }
    }
    
    return prot;
}

void MemoryMap::applySmartFilter() {
   
    // 首先将所有区域设为不过滤
    for (auto* region : memoryRegions_) {
        region->isFilterable = false;
    }
    
    // 分析已加载模块，智能调整过滤规则
    for (auto* region : memoryRegions_) {
        //size_t regionSize = region->endAddress - region->startAddress;
        
        // 智能过滤规则
        // 1. 代码段为基质头，保留
        if (region->type == Code_app ) {
            region->isFilterable = false;
        }
        
        // 2. Java堆区域，过滤
        else if (region->type == Java_heap) {
            region->isFilterable = true;
        }
        
        // // 3. 大型匿名区域（大于50MB）可能是图像缓冲区等，过滤
        // else if (region->type == Anonymous && regionSize > 50 * 1024 * 1024) {
        //     region->isFilterable = true;
        // }
        
        // 4. 栈区域保留
        else if (region->type == Stack) {
            region->isFilterable = false;
        }
        
        // 5. 共享内存区域通常不包含指针链，过滤
        else if (region->type == Ashmem) {
            region->isFilterable = true;
        }
        
        // 6. 可能是主模块数据段，保留
        else if (region->type == C_data ) {
            region->isFilterable = false;
        }
    }
    
}

} // namespace memchainer
