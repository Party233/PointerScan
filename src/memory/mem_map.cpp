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
std::vector<MemoryRegion*> memoryRegionList;//待扫描数据段
std::vector<MemoryRegion*> staticRegionList;//静态区域段


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

    for (auto* region : memoryRegions_) {
       
        //unknow全部过滤
        if (region->type==Unknown) {
          region->isFilterable=true;
          continue;
        }
        result.push_back(region);
    
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
        //memoryRegionList.push_back(region); // 添加到全局列表
    }
    
    return true;
}

bool MemoryMap::parseProcessModule() {
    
    if (currentPid_ <= 0) {
        return false;
    }
    
    //printRegionInfo();
    //静态区域 xa cb（bss cd
    auto  static_type = MemoryRegionType::Code_app | MemoryRegionType::C_data ;

       staticRegionList.clear();
    for (auto* region : memoryRegions_) {
        //处理名字 获取最后一个/后面的部分
        std::string name = region->name;
        auto pos = name.find_last_of("/");
        if (pos!=std::string::npos)
        {
           name = name.substr(pos+1);
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

//只需分出 a ca cb cd xa o？ 其他全扔unknow用不上
int MemoryMap::determineRegionType(const std::string& name, const std::string& permissions) {
  
    // 匿名映射 || isFind(name, "[anon:")
 if ((name.empty()  || name == " ")&&permissions[0]=='r') {
    return MemoryRegionType::Anonymous;
    }
    else if (isFind(name, "[anon:libc_malloc")|| isFind(name, "[anon:scudo:"))
    {
        return MemoryRegionType::C_alloc;
    } 
    //cd
    else if (isFind(name, "/data/app/")  && isFind(permissions, "xp") && isFind(name, ".so"))
    {
        return MemoryRegionType::Code_app;
    }
    else if (isFind(name, "[anon:.bss]"))
    {
        return MemoryRegionType::C_bss;
    }
    else if (isFind(name, "/data/app/") && isFind(name, ".so"))
    {
        return MemoryRegionType::C_data;
    }


    
    return MemoryRegionType::Unknown;
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



} // namespace memchainer
