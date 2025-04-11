#pragma once

#include "common/types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <memory>
#include <mutex>
#include <functional>

namespace memchainer {

// 范围索引条目
struct RangeEntry {
    Address startValue;        // 范围起始值
    Address endValue;          // 范围结束值
    int64_t fileOffset;        // 文件中的偏移
    int64_t entryCount;        // 条目数量
};

// 指针缓存条目
struct PointerCacheEntry {
    Address address;           // 指针地址
    Address value;             // 指针值
    Offset offset;             // 偏移量
};

// 基于文件的缓存系统
class FileCache {
public:
    // 进度回调函数类型
    using ProgressCallback = std::function<void(float progress)>;
    
    static constexpr size_t FILE_BUFFER_SIZE = 4 * 1024 * 1024; // 4MB
    static constexpr size_t RANGE_BUCKET_SIZE = 1024 * 1024;    // 1MB
    
    FileCache();
    ~FileCache();

    // 初始化缓存系统，创建临时文件
    bool initialize(const std::string& cacheDir = "");

    // 开始写入新的缓存数据
    bool beginWriteCache(int level);

    // 添加指针数据到缓存
    bool addPointerToCache(Address address, Address value, Offset offset = 0);

    // 完成缓存写入，构建索引
    bool endWriteCache();

    // 查找特定值范围内的指针（用于扫描时）
    std::vector<PointerCacheEntry> findPointersInRange(Address minValue, Address maxValue);

    // 清理所有缓存文件
    void cleanup();

    // 获取当前等级的指针总数
    size_t getCurrentLevelPointerCount() const;

    // 设置扫描进度回调
    void setProgressCallback(const ProgressCallback& callback) {
        progressCallback_ = callback;
    }

    // 通过偏移量读取指针
    PointerCacheEntry* readPointerByOffset(int64_t offset);

private:
    // 构建范围索引
    bool buildRangeIndex();
    
    // 分块构建范围索引
    bool buildRangeIndexInChunks(std::ifstream& dataFile, size_t fileSize, size_t entryCount);
    
    // 使用更小的分块构建范围索引（当内存受限时）
    bool buildRangeIndexWithSmallChunks(std::ifstream& dataFile, size_t fileSize, size_t entryCount);
    
    // 私有方法：重载的findPointersInRange，实际执行搜索，结果存储在currentSearchResults_
    bool findPointersInRange_(Address minValue, Address maxValue);
    
    // 获取临时文件路径
    std::string getTempFilePath(int level) const;
    
    // 获取索引文件路径
    std::string getIndexFilePath(int level) const;
    
    // 从文件加载索引
    bool loadIndex(int level);

    std::string cacheDir_;                         // 缓存目录
    int currentLevel_;                             // 当前处理等级
    size_t currentLevelPointerCount_;              // 当前等级指针数量
    std::ofstream dataFile_;                       // 数据文件流
    std::vector<RangeEntry> rangeIndex_;           // 范围索引
    std::unordered_map<int, std::string> dataFiles_; // 数据文件路径映射
    mutable std::mutex mutex_;                     // 同步锁
    ProgressCallback progressCallback_;            // 进度回调
    
    // 缓存常量
    static constexpr size_t MAX_MEMORY_BUFFER = 50 * 1024 * 1024; // 50MB内存缓冲区上限
    std::vector<PointerCacheEntry> currentSearchResults_; // 当前搜索结果缓存
};

} // namespace memchainer 