#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <list>
#include <algorithm>
#include <cstring>

namespace memchainer {

// 使用明确大小的类型
using Address = uint64_t;
using Offset = int64_t;
using MemorySize = uint64_t;
using ProcessId = int32_t;

// 内存区域类型枚举 - 修改为 enum 而不是 enum class，与原项目保持一致
enum MemoryRegionType : int {
    All = -1,
    Unknown = 0,
    Anonymous = 1 << 5,//32 匿名内存
    C_alloc = 1 << 2,//4 C分配内存
    C_heap = 1 << 0,//1 C堆内存
    C_data = 1 << 4,//16 C数据内存
    C_bss = 1 << 3,//8 C未初始化数据内存
    Java_heap = 1 << 1,//2 Java堆内存
    Java = 1 << 16,//65536 Java内存
    Stack = 1 << 6,//64 栈内存
    Video = 1 << 20,//1048576 视频内存
    Code_app = 1 << 14,//16384 应用程序代码内存
    Code_system = 1 << 15,//32768 系统代码内存
    Ashmem = 1 << 19,//524288 Ashmem共享内存
    Bad = 1 << 17,//32768 坏内存
    Other = -2080896,//-2080896 其他内存
    PPSSPP = 1 << 18,//262144 PPSSPP内存

};

// 内存区域描述
struct MemoryRegion {
    Address startAddress;
    Address endAddress;
    int type;
    int count;          // 用于区分同名区域
    char name[128];      // 固定大小的名称
    bool isFilterable;  // 是否可过滤

    MemoryRegion(Address start, Address end, int t = 0, const char* n = "", int c = 0, bool filter = false)
        : startAddress(start), endAddress(end), type(t), count(c), isFilterable(filter) {
        if (n) {
            strncpy(name, n, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
        } else {
            name[0] = '\0';
        }
    }
};

// 全局内存区域列表
extern std::vector<MemoryRegion*> memoryRegionList;
extern std::vector<MemoryRegion*> staticRegionList;

// 指针数据结构
struct PointerData {
    Address address;      // 指针地址
    Address value;        // 指针指向的值
    Offset offset;        // 相对偏移

    PointerData(Address addr, Address val, Offset off = 0)
        : address(addr), value(val), offset(off) {}
};

struct StaticOffset
{
    uint64_t staticOffset; // 静态偏移量
    MemoryRegion* region; // 内存区域
    StaticOffset(uint64_t staticOff, MemoryRegion* reg)
        : staticOffset(staticOff), region(reg) {}
    StaticOffset()
        : staticOffset(0), region(nullptr) {}
};



// 指针数据结构
struct PointerAllData {
    Address address;      // 指针地址
    Address value;        // 指针指向的值
    Address startAddress; // 起始地址
    Offset offset;        // 相对偏移
    StaticOffset staticOffset_; // 静态偏移量

    PointerAllData(Address addr, Address val, Address start, Offset off = 0, StaticOffset staticOff = StaticOffset(0, nullptr))
        : address(addr), value(val), startAddress(start), offset(off), staticOffset_(staticOff) {}
};

// 指针方向结构
struct PointerDir {
    Address value;        // 指针值
    Address address;      // 地址
    Offset offset;        // 偏移量
    StaticOffset staticOffset_; // 静态偏移量
    PointerDir* child;    // 指向子节点的指针（下一层） 因为是从底往上 所以这个其实是父节点

    PointerDir(Address val = 0, Address addr = 0, Offset off = 0, StaticOffset staticOff = StaticOffset(0, nullptr))
        : value(val), address(addr), offset(off), staticOffset_(staticOff), child(nullptr) {};
    PointerDir(Address val = 0, Address addr = 0,PointerDir* c = nullptr)
        : value(val), address(addr) {
            offset = 0;
            staticOffset_ = StaticOffset(0, nullptr);
            child = c;
        };

};

// 指针范围结构
struct PointerRange {
    int level;            // 层级
    Address address;      // 地址
    std::vector<PointerDir> results; // 地址对应的指针列表

    PointerRange(int lvl, Address addr, std::vector<PointerDir>&& res = {})
        : level(lvl), address(addr), results(std::move(res)) {}
};

// 简化的指针链结构，用于最终结果输出
struct SimplePointerChain {
    std::vector<PointerDir*> nodes;  // 从静态指针到目标地址的节点
    Address targetAddress;          // 目标地址
    Address baseAddress;            // 基址
    MemoryRegion* baseRegion;       // 基址所在区域
    uint64_t staticOffset;          // 静态偏移

    SimplePointerChain() : targetAddress(0), baseAddress(0), baseRegion(nullptr), staticOffset(0) {}
};



// 链信息结构
template <typename T>
struct ChainInfo {
    std::vector<std::vector<T>> counts;
    std::vector<std::vector<PointerDir*>> contents;
};



} // namespace memchainer
