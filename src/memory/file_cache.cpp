#include "memory/file_cache.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <chrono>

namespace fs = std::filesystem;
namespace memchainer {

FileCache::FileCache() 
    : currentLevel_(-1), currentLevelPointerCount_(0) {
}

FileCache::~FileCache() {
    if (dataFile_.is_open()) {
        dataFile_.close();
    }
}

bool FileCache::initialize(const std::string &cacheDir)
{

    {
        std::lock_guard<std::mutex> lock(mutex_);
        // 设置缓存目录
        if (cacheDir.empty())
        {
            cacheDir_ = fs::temp_directory_path().string() + "/chainer_cache";
        }
        else
        {
            cacheDir_ = cacheDir;
        }
        // 确保目录存在
        try
        {
            if (!fs::exists(cacheDir_))
            {
                fs::create_directories(cacheDir_);
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "创建缓存目录失败: " << e.what() << std::endl;
            return false;
        }
    }

    std::cout << "临时文件缓存目录：" << cacheDir_ << std::endl;

    // 清理之前的缓存文件
    cleanup();

    return true;
}

bool FileCache::beginWriteCache(int level) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 关闭之前的文件（如果有）
    if (dataFile_.is_open()) {
        dataFile_.close();
    }
    
    currentLevel_ = level;
    currentLevelPointerCount_ = 0;
    
    // 创建新的数据文件
    std::string filePath = getTempFilePath(level);
    dataFile_.open(filePath, std::ios::binary | std::ios::out);
    if (!dataFile_) {
        std::cerr << "无法创建缓存文件: " << filePath << std::endl;
        return false;
    }
    
    // 设置文件缓冲区
    dataFile_.rdbuf()->pubsetbuf(new char[FILE_BUFFER_SIZE], FILE_BUFFER_SIZE);
    
    // 记录文件路径
    dataFiles_[level] = filePath;
    
    std::cout << "开始缓存层级 " << level << " 的指针数据" << std::endl;
    
    return true;
}

bool FileCache::addPointerToCache(Address address, Address value, Offset offset) {
    if (!dataFile_.is_open()) {
        return false;
    }
    
    // 写入指针数据
    PointerCacheEntry entry{address, value, offset};
    dataFile_.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
    
    if (!dataFile_) {
        std::cerr << "写入缓存数据失败" << std::endl;
        return false;
    }
    
    currentLevelPointerCount_++;
    
    // 每百万个指针显示一次进度
    if (currentLevelPointerCount_ % 1000000 == 0) {
        std::cout << "已缓存 " << (currentLevelPointerCount_ / 1000000) << "M 个指针" << std::endl;
        
        // 调用进度回调（如果有）
        if (progressCallback_) {
            progressCallback_(static_cast<float>(currentLevelPointerCount_) / 
                             static_cast<float>(std::numeric_limits<size_t>::max()));
        }
    }
    
    return true;
}

bool FileCache::endWriteCache() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!dataFile_.is_open()) {
        return false;
    }
    
    // 确保所有数据都写入文件
    dataFile_.flush();
    
    // 关闭文件
    dataFile_.close();
    
    // 构建索引
    if (!buildRangeIndex()) {
        std::cerr << "构建范围索引失败" << std::endl;
        return false;
    }
    
    std::cout << "层级 " << currentLevel_ << " 的指针数据缓存完成，共 " 
              << currentLevelPointerCount_ << " 个指针" << std::endl;
    
    return true;
}

bool FileCache::findPointersInRange_(Address minValue, Address maxValue) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 检查文件路径是否有效
    std::string filePath = dataFiles_[currentLevel_];
    if (filePath.empty()) {
        std::cerr << "无效的缓存文件路径" << std::endl;
        return false;
    }
    
    // 检查索引是否已加载
    if (rangeIndex_.empty()) {
        if (!loadIndex(currentLevel_)) {
            std::cerr << "加载范围索引失败" << std::endl;
            return false;
        }
    }
    
    // 查找匹配的范围
    std::vector<RangeEntry> matchedRanges;
    
    // 使用二分查找找到第一个可能包含minValue的范围
    auto startIt = std::lower_bound(rangeIndex_.begin(), rangeIndex_.end(), minValue,
                                  [](const RangeEntry& entry, Address value) {
                                      return entry.endValue < value;
                                  });
    
    // 使用二分查找找到最后一个可能包含maxValue的范围
    auto endIt = std::upper_bound(rangeIndex_.begin(), rangeIndex_.end(), maxValue,
                                [](Address value, const RangeEntry& entry) {
                                    return value < entry.startValue;
                                });
    
    // 收集所有匹配的范围
    for (auto it = startIt; it != endIt; ++it) {
        matchedRanges.push_back(*it);
    }
    
    // 没有找到匹配的范围，但这不是错误
    if (matchedRanges.empty()) {
        std::cout << "未找到值范围 [" << std::hex << minValue << " - " 
                 << maxValue << std::dec << "] 的指针" << std::endl;
        return true;
    }
    
    // 打开数据文件
    std::ifstream dataFile(filePath, std::ios::binary | std::ios::in);
    if (!dataFile) {
        std::cerr << "无法打开缓存文件: " << filePath << std::endl;
        return false;
    }
    
    // 获取文件大小
    dataFile.seekg(0, std::ios::end);
    size_t fileSize = dataFile.tellg();
    dataFile.seekg(0, std::ios::beg);
    
    // 文件太小，无法包含任何条目
    if (fileSize < sizeof(PointerCacheEntry)) {
        std::cerr << "缓存文件大小异常: " << fileSize << " 字节" << std::endl;
        return false;
    }
    
    // 设置文件缓冲区
    char* buffer = new char[FILE_BUFFER_SIZE];
    dataFile.rdbuf()->pubsetbuf(buffer, FILE_BUFFER_SIZE);
    
    bool success = true;
    
    // 从每个匹配的范围中读取数据
    for (const auto& range : matchedRanges) {
        if (range.entryCount <= 0) {
            continue; // 跳过空范围
        }
        
        // 检查文件偏移是否有效
        if (range.fileOffset < 0) {
            std::cerr << "无效的文件偏移: " << range.fileOffset << std::endl;
            continue;
        }
        
        // 安全检查：确保要读取的数据不会超出文件范围
        size_t rangeEndOffset = range.fileOffset + range.entryCount * sizeof(PointerCacheEntry);
        if (rangeEndOffset > fileSize) {
            std::cerr << "范围超出文件大小: 范围结束于 " << rangeEndOffset 
                     << "，文件大小为 " << fileSize << std::endl;
            success = false;
            continue;
        }
        
        // 跳转到范围的起始位置
        dataFile.seekg(range.fileOffset);
        if (dataFile.fail()) {
            std::cerr << "跳转到文件位置失败: " << range.fileOffset << std::endl;
            dataFile.clear(); // 清除错误状态
            success = false;
            continue;
        }
        
        // 分配足够大的缓冲区
        std::vector<PointerCacheEntry> rangeEntries;
        try {
            rangeEntries.resize(range.entryCount);
        } catch (const std::exception& e) {
            std::cerr << "无法分配内存用于读取范围条目: " << e.what() 
                     << "，条目数: " << range.entryCount << std::endl;
            success = false;
            continue;
        }
        
        // 读取范围内的所有条目
        dataFile.read(reinterpret_cast<char*>(rangeEntries.data()), 
                     range.entryCount * sizeof(PointerCacheEntry));
        
        if (dataFile.fail()) {
            std::cerr << "读取范围内容失败，范围: [" << std::hex << range.startValue << " - " 
                      << range.endValue << std::dec << "], 偏移: " << range.fileOffset 
                      << ", 条目数: " << range.entryCount << std::endl;
            dataFile.clear(); // 清除错误状态
            success = false;
            continue;
        }
        
        // 过滤满足条件的条目
        for (const auto& entry : rangeEntries) {
            if (entry.value >= minValue && entry.value <= maxValue) {
                currentSearchResults_.push_back(entry);
            }
        }
    }
    
    dataFile.close();
    delete[] buffer;
    
    std::cout << "找到 " << currentSearchResults_.size() << " 个符合条件的指针" << std::endl;
    
    return success;
}

std::vector<PointerCacheEntry> FileCache::findPointersInRange(Address minValue, Address maxValue) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 清空现有搜索结果
    currentSearchResults_.clear();
    
    // 执行搜索
    bool success = findPointersInRange_(minValue, maxValue);
    
    if (!success) {
        std::cerr << "查找指针过程中发生错误" << std::endl;
    }
    
    // 返回结果副本
    return currentSearchResults_;
}

PointerCacheEntry* FileCache::readPointerByOffset(int64_t offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 检查文件路径是否有效
    std::string filePath = dataFiles_[currentLevel_];
    if (filePath.empty()) {
        std::cerr << "无效的缓存文件路径" << std::endl;
        return nullptr;
    }
    
    // 检查偏移量是否合法
    if (offset < 0) {
        std::cerr << "无效的偏移量: " << offset << std::endl;
        return nullptr;
    }
    
    // 打开文件
    std::ifstream dataFile(filePath, std::ios::binary | std::ios::in);
    if (!dataFile) {
        std::cerr << "无法打开缓存文件: " << filePath << std::endl;
        return nullptr;
    }
    
    // 获取文件大小
    dataFile.seekg(0, std::ios::end);
    size_t fileSize = dataFile.tellg();
    dataFile.seekg(0, std::ios::beg);
    
    // 检查文件大小
    if (fileSize == 0) {
        std::cerr << "缓存文件为空" << std::endl;
        return nullptr;
    }
    
    // 确保偏移量不超出文件范围
    if (offset + sizeof(PointerCacheEntry) > fileSize) {
        std::cerr << "偏移量超出文件范围: " << offset << " + " << sizeof(PointerCacheEntry) 
                 << " > " << fileSize << std::endl;
        return nullptr;
    }
    
    // 跳转到指定位置
    dataFile.seekg(offset);
    if (dataFile.fail()) {
        std::cerr << "跳转到文件位置失败: " << offset << std::endl;
        return nullptr;
    }
    
    // 读取指针数据
    static PointerCacheEntry entry;
    dataFile.read(reinterpret_cast<char*>(&entry), sizeof(entry));
    
    if (dataFile.fail()) {
        std::cerr << "读取指针数据失败: 偏移量 " << offset << std::endl;
        return nullptr;
    }
    
    return &entry;
}

bool FileCache::buildRangeIndex() {
    // 清空现有索引
    rangeIndex_.clear();
    
    // 打开数据文件进行读取
    std::string filePath = dataFiles_[currentLevel_];
    std::ifstream dataFile(filePath, std::ios::binary | std::ios::in);
    if (!dataFile) {
        std::cerr << "无法打开缓存文件: " << filePath << std::endl;
        return false;
    }
    
    // 设置文件缓冲区
    char* buffer = new char[FILE_BUFFER_SIZE];
    dataFile.rdbuf()->pubsetbuf(buffer, FILE_BUFFER_SIZE);
    
    // 获取文件大小
    dataFile.seekg(0, std::ios::end);
    size_t fileSize = dataFile.tellg();
    dataFile.seekg(0, std::ios::beg);
    
    // 如果文件为空或小于一个条目的大小，返回成功但不创建索引
    if (fileSize == 0 || fileSize < sizeof(PointerCacheEntry)) {
        std::cout << "缓存文件为空或不包含完整条目" << std::endl;
        delete[] buffer;
        
        // 创建一个空索引文件
        std::string indexPath = getIndexFilePath(currentLevel_);
        std::ofstream indexFile(indexPath, std::ios::binary | std::ios::out);
        if (indexFile) {
            indexFile.close();
        }
        
        // 确保有一个范围索引，即使是空的
        RangeEntry emptyRange{0, 0, 0, 0};
        rangeIndex_.push_back(emptyRange);
        
        return true;
    }
    
    // 确保文件大小是条目大小的整数倍
    if (fileSize % sizeof(PointerCacheEntry) != 0) {
        std::cerr << "文件大小不是条目大小的整数倍: " << fileSize << " % " 
                 << sizeof(PointerCacheEntry) << " = " 
                 << (fileSize % sizeof(PointerCacheEntry)) << std::endl;
        
        // 调整为最接近的整数倍
        size_t validFileSize = (fileSize / sizeof(PointerCacheEntry)) * sizeof(PointerCacheEntry);
        std::cerr << "将使用有效部分: " << validFileSize << " 字节" << std::endl;
        fileSize = validFileSize;
    }
    
    // 计算条目数量
    size_t entryCount = fileSize / sizeof(PointerCacheEntry);
    if (entryCount == 0) {
        std::cout << "没有有效的条目" << std::endl;
        delete[] buffer;
        
        // 创建空索引
        RangeEntry emptyRange{0, 0, 0, 0};
        rangeIndex_.push_back(emptyRange);
        
        std::string indexPath = getIndexFilePath(currentLevel_);
        std::ofstream indexFile(indexPath, std::ios::binary | std::ios::out);
        if (indexFile) {
            indexFile.write(reinterpret_cast<const char*>(&emptyRange), sizeof(emptyRange));
            indexFile.close();
        }
        
        return true;
    }
    
    // 先读取所有指针并按值排序
    std::vector<std::pair<Address, int64_t>> valueOffsetPairs;
    
    std::cout << "读取指针数据进行排序..." << std::endl;

    // 根据文件大小预分配空间
    try {
        valueOffsetPairs.reserve(entryCount);
    } catch (const std::exception& e) {
        std::cerr << "无法为值-偏移对分配内存: " << e.what() 
                 << "，条目数: " << entryCount << std::endl;
        delete[] buffer;
        return false;
    }
    
    // 尝试一次性读取所有数据到内存
    std::vector<PointerCacheEntry> allEntries;
    try {
        allEntries.resize(entryCount);
    } catch (const std::exception& e) {
        std::cerr << "无法为所有条目分配内存: " << e.what() 
                 << "，条目数: " << entryCount << std::endl;
        
        // 如果一次性分配失败，则改为分块读取
        delete[] buffer;
        return buildRangeIndexInChunks(dataFile, fileSize, entryCount);
    }
    
    // 读取所有数据
    dataFile.read(reinterpret_cast<char*>(allEntries.data()), fileSize);
    
    // 检查读取是否成功
    if (dataFile.fail()) {
        std::cerr << "读取缓存文件数据失败" << std::endl;
        
        // 尝试分块构建
        dataFile.clear();
        dataFile.seekg(0, std::ios::beg);
        
        delete[] buffer;
        return buildRangeIndexInChunks(dataFile, fileSize, entryCount);
    }
    
    // 构建偏移对
    for (size_t i = 0; i < entryCount; i++) {
        valueOffsetPairs.emplace_back(allEntries[i].value, i * sizeof(PointerCacheEntry));
        
        // 每读取100万个，显示进度
        if (valueOffsetPairs.size() % 1000000 == 0) {
            std::cout << "已读取 " << (valueOffsetPairs.size() / 1000000) << "M 个指针" << std::endl;
            
            // 调用进度回调（如果有）
            if (progressCallback_) {
                progressCallback_(static_cast<float>(valueOffsetPairs.size()) / 
                                 static_cast<float>(entryCount));
            }
        }
    }
    
    // 释放allEntries内存，不再需要
    allEntries.clear();
    allEntries.shrink_to_fit();
    
    if (valueOffsetPairs.empty()) {
        std::cout << "没有找到有效的指针数据" << std::endl;
        
        // 创建空索引
        RangeEntry emptyRange{0, 0, 0, 0};
        rangeIndex_.push_back(emptyRange);
        
        std::string indexPath = getIndexFilePath(currentLevel_);
        std::ofstream indexFile(indexPath, std::ios::binary | std::ios::out);
        if (indexFile) {
            indexFile.write(reinterpret_cast<const char*>(&emptyRange), sizeof(emptyRange));
            indexFile.close();
        }
        
        delete[] buffer;
        return true;
    }
    
    std::cout << "开始排序 " << valueOffsetPairs.size() << " 个指针..." << std::endl;
    
    // 按值排序
    try {
        std::sort(valueOffsetPairs.begin(), valueOffsetPairs.end(), 
                [](const auto& a, const auto& b) {
                    return a.first < b.first;
                });
    } catch (const std::exception& e) {
        std::cerr << "排序指针时发生异常: " << e.what() << std::endl;
        delete[] buffer;
        return false;
    }
    
    std::cout << "排序完成，创建范围表..." << std::endl;
    
    // 创建范围索引文件
    std::string indexPath = getIndexFilePath(currentLevel_);
    std::ofstream indexFile(indexPath, std::ios::binary | std::ios::out);
    if (!indexFile) {
        std::cerr << "无法创建索引文件: " << indexPath << std::endl;
        delete[] buffer;
        return false;
    }
    
    // 如果值对为空，创建一个空索引
    if (valueOffsetPairs.empty()) {
        RangeEntry emptyRange{0, 0, 0, 0};
        rangeIndex_.push_back(emptyRange);
        indexFile.write(reinterpret_cast<const char*>(&emptyRange), sizeof(emptyRange));
        indexFile.close();
        
        std::cout << "没有指针数据，创建空索引" << std::endl;
        delete[] buffer;
        return true;
    }
    
    // 创建排序后的数据文件
    std::string sortedFilePath = filePath + ".sorted";
    std::ofstream sortedFile(sortedFilePath, std::ios::binary | std::ios::out);
    if (!sortedFile) {
        std::cerr << "无法创建排序后的数据文件: " << sortedFilePath << std::endl;
        delete[] buffer;
        return false;
    }
    
    // 设置文件缓冲区
    char* sortedBuffer = new char[FILE_BUFFER_SIZE];
    sortedFile.rdbuf()->pubsetbuf(sortedBuffer, FILE_BUFFER_SIZE);
    
    // 按排序后的顺序写入数据
    std::cout << "创建排序后的数据文件..." << std::endl;
    
    Address currentRangeStart = valueOffsetPairs[0].first;
    Address currentRangeEnd = currentRangeStart;
    int64_t rangeStartOffset = 0;
    int64_t entryCounter = 0;
    
    // 临时缓冲区，用于读取单个条目
    PointerCacheEntry readEntry;
    
    for (size_t i = 0; i < valueOffsetPairs.size(); ++i) {
        const auto& [value, fileOffset] = valueOffsetPairs[i];
        
        // 从原文件读取数据
        dataFile.seekg(fileOffset);
        if (dataFile.fail()) {
            std::cerr << "跳转到文件位置失败: " << fileOffset << std::endl;
            dataFile.clear();
            continue;
        }
        
        dataFile.read(reinterpret_cast<char*>(&readEntry), sizeof(readEntry));
        if (dataFile.fail()) {
            std::cerr << "读取指针数据失败，偏移: " << fileOffset << std::endl;
            dataFile.clear();
            continue;
        }
        
        // 写入排序后的文件
        sortedFile.write(reinterpret_cast<const char*>(&readEntry), sizeof(readEntry));
        if (sortedFile.fail()) {
            std::cerr << "写入排序后的文件失败" << std::endl;
            continue;
        }
        
        // 更新范围结束值
        currentRangeEnd = value;
        entryCounter++;
        
        // 检查是否达到范围桶大小或者是最后一条数据
        bool isLastEntry = (i == valueOffsetPairs.size() - 1);
        bool newRangeNeeded = ((value - currentRangeStart) >= RANGE_BUCKET_SIZE) || isLastEntry;
        
        if (newRangeNeeded) {
            // 创建范围条目
            RangeEntry rangeEntry{
                currentRangeStart,
                currentRangeEnd,
                rangeStartOffset,
                entryCounter
            };
            
            // 添加到内存中的范围索引
            rangeIndex_.push_back(rangeEntry);
            
            // 写入索引文件
            indexFile.write(reinterpret_cast<const char*>(&rangeEntry), sizeof(rangeEntry));
            if (indexFile.fail()) {
                std::cerr << "写入索引文件失败" << std::endl;
            }
            
            // 为下一个范围做准备
            if (!isLastEntry) {
                currentRangeStart = value;
                rangeStartOffset += entryCounter * sizeof(PointerCacheEntry);
                entryCounter = 0;
            }
            
            // 每创建10个范围，显示一次进度
            if (rangeIndex_.size() % 10 == 0) {
                std::cout << "已创建 " << rangeIndex_.size() << " 个范围索引" << std::endl;
            }
        }
    }
    
    // 关闭文件
    dataFile.close();
    indexFile.close();
    sortedFile.close();
    
    // 释放缓冲区
    delete[] buffer;
    delete[] sortedBuffer;
    
    // 确保至少有一个范围索引
    if (rangeIndex_.empty()) {
        RangeEntry rangeEntry{0, 0, 0, 0};
        
        if (!valueOffsetPairs.empty()) {
            rangeEntry.startValue = valueOffsetPairs.front().first;
            rangeEntry.endValue = valueOffsetPairs.back().first;
            rangeEntry.fileOffset = 0;
            rangeEntry.entryCount = static_cast<int64_t>(valueOffsetPairs.size());
        }
        
        rangeIndex_.push_back(rangeEntry);
        
        // 重新打开索引文件并写入
        std::ofstream fixIndexFile(indexPath, std::ios::binary | std::ios::out);
        if (fixIndexFile) {
            fixIndexFile.write(reinterpret_cast<const char*>(&rangeEntry), sizeof(rangeEntry));
            fixIndexFile.close();
        }
    }
    
    // 用排序后的文件替换原文件
    try {
        fs::remove(filePath);
        fs::rename(sortedFilePath, filePath);
    } catch (const std::exception& e) {
        std::cerr << "替换文件失败: " << e.what() << std::endl;
        return false;
    }
    
    std::cout << "范围索引构建完成，共 " << rangeIndex_.size() << " 个范围" << std::endl;
    return true;
}

bool FileCache::buildRangeIndexInChunks(std::ifstream& dataFile, size_t fileSize, size_t entryCount) {
    std::cout << "使用分块方式构建范围索引..." << std::endl;
    
    // 重置文件位置
    dataFile.seekg(0, std::ios::beg);
    if (dataFile.fail()) {
        std::cerr << "重置文件位置失败" << std::endl;
        return false;
    }
    
    // 创建排序后的数据文件
    std::string filePath = dataFiles_[currentLevel_];
    std::string sortedFilePath = filePath + ".sorted";
    std::ofstream sortedFile(sortedFilePath, std::ios::binary | std::ios::out);
    if (!sortedFile) {
        std::cerr << "无法创建排序后的数据文件: " << sortedFilePath << std::endl;
        return false;
    }
    
    // 创建索引文件
    std::string indexPath = getIndexFilePath(currentLevel_);
    std::ofstream indexFile(indexPath, std::ios::binary | std::ios::out);
    if (!indexFile) {
        std::cerr << "无法创建索引文件: " << indexPath << std::endl;
        return false;
    }
    
    // 设置缓冲区
    char* sortedBuffer = new char[FILE_BUFFER_SIZE];
    sortedFile.rdbuf()->pubsetbuf(sortedBuffer, FILE_BUFFER_SIZE);
    
    // 分块大小，每块最多读取10万个条目
    constexpr size_t CHUNK_SIZE = 100000;
    const size_t totalChunks = (entryCount + CHUNK_SIZE - 1) / CHUNK_SIZE;
    
    std::cout << "总条目数: " << entryCount << ", 分为 " << totalChunks << " 个块" << std::endl;
    
    // 预估每个块需要的内存
    const size_t chunkMemory = CHUNK_SIZE * sizeof(PointerCacheEntry) + 
                             CHUNK_SIZE * sizeof(std::pair<Address, int64_t>);
    
    if (chunkMemory > 2 * 1024 * 1024 * 1024ULL) {
        std::cerr << "块大小过大，可能导致内存问题: " << 
                  (chunkMemory / (1024 * 1024)) << "MB" << std::endl;
        
        // 调整块大小为更小的值
        return buildRangeIndexWithSmallChunks(dataFile, fileSize, entryCount);
    }
    
    // 临时变量存储全局范围信息
    Address globalMinValue = std::numeric_limits<Address>::max();
    Address globalMaxValue = 0;
    std::vector<std::pair<Address, RangeEntry>> sortedRanges;
    
    // 处理每个块
    for (size_t chunkIndex = 0; chunkIndex < totalChunks; ++chunkIndex) {
        // 计算当前块的大小
        const size_t startEntry = chunkIndex * CHUNK_SIZE;
        const size_t chunkEntryCount = std::min(CHUNK_SIZE, entryCount - startEntry);
        
        std::cout << "处理块 " << (chunkIndex + 1) << "/" << totalChunks 
                  << " (条目: " << startEntry << " - " << (startEntry + chunkEntryCount - 1) << ")" << std::endl;
        
        // 定位到块的起始位置
        const int64_t chunkOffset = startEntry * sizeof(PointerCacheEntry);
        dataFile.seekg(chunkOffset);
        if (dataFile.fail()) {
            std::cerr << "跳转到块位置失败: " << chunkOffset << std::endl;
            dataFile.clear();
            continue;
        }
        
        // 读取块数据
        std::vector<PointerCacheEntry> chunkEntries(chunkEntryCount);
        dataFile.read(reinterpret_cast<char*>(chunkEntries.data()), 
                     chunkEntryCount * sizeof(PointerCacheEntry));
        
        if (dataFile.fail()) {
            std::cerr << "读取块数据失败" << std::endl;
            dataFile.clear();
            continue;
        }
        
        // 创建值-偏移对
        std::vector<std::pair<Address, int64_t>> chunkValueOffsetPairs;
        chunkValueOffsetPairs.reserve(chunkEntryCount);
        
        for (size_t i = 0; i < chunkEntryCount; ++i) {
            const int64_t entryOffset = chunkOffset + i * sizeof(PointerCacheEntry);
            chunkValueOffsetPairs.emplace_back(chunkEntries[i].value, entryOffset);
            
            // 更新全局范围
            globalMinValue = std::min(globalMinValue, chunkEntries[i].value);
            globalMaxValue = std::max(globalMaxValue, chunkEntries[i].value);
        }
        
        // 排序当前块
        try {
            std::sort(chunkValueOffsetPairs.begin(), chunkValueOffsetPairs.end(),
                    [](const auto& a, const auto& b) {
                        return a.first < b.first;
                    });
        } catch (const std::exception& e) {
            std::cerr << "排序块时发生异常: " << e.what() << std::endl;
            continue;
        }
        
        // 如果块为空，继续下一个
        if (chunkValueOffsetPairs.empty()) {
            continue;
        }
        
        // 创建范围（每个块至少创建一个范围）
        Address chunkMinValue = chunkValueOffsetPairs.front().first;
        Address chunkMaxValue = chunkValueOffsetPairs.back().first;
        
        // 计算此块在排序后文件中的位置
        int64_t sortedFileOffset = sortedFile.tellp();
        
        // 写入排序后的数据
        for (const auto& [value, origOffset] : chunkValueOffsetPairs) {
            // 重新读取原始数据
            dataFile.seekg(origOffset);
            if (dataFile.fail()) {
                std::cerr << "跳转到条目位置失败: " << origOffset << std::endl;
                dataFile.clear();
                continue;
            }
            
            PointerCacheEntry entry;
            dataFile.read(reinterpret_cast<char*>(&entry), sizeof(entry));
            if (dataFile.fail()) {
                std::cerr << "读取条目数据失败" << std::endl;
                dataFile.clear();
                continue;
            }
            
            // 写入排序后的文件
            sortedFile.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
            if (sortedFile.fail()) {
                std::cerr << "写入排序后的文件失败" << std::endl;
                break;
            }
        }
        
        // 创建范围条目
        RangeEntry rangeEntry{
            chunkMinValue,
            chunkMaxValue,
            sortedFileOffset,
            static_cast<int64_t>(chunkValueOffsetPairs.size())
        };
        
        // 保存到排序后的范围列表
        sortedRanges.emplace_back(chunkMinValue, rangeEntry);
    }
    
    // 确保范围按值排序
    std::sort(sortedRanges.begin(), sortedRanges.end(),
            [](const auto& a, const auto& b) {
                return a.first < b.first;
            });
    
    // 写入所有范围到索引文件并保存到内存
    rangeIndex_.clear();
    
    if (sortedRanges.empty()) {
        // 创建空范围
        RangeEntry emptyRange{globalMinValue, globalMaxValue, 0, 0};
        rangeIndex_.push_back(emptyRange);
        indexFile.write(reinterpret_cast<const char*>(&emptyRange), sizeof(emptyRange));
    } else {
        for (const auto& [_, rangeEntry] : sortedRanges) {
            rangeIndex_.push_back(rangeEntry);
            indexFile.write(reinterpret_cast<const char*>(&rangeEntry), sizeof(rangeEntry));
        }
    }
    
    // 关闭文件
    sortedFile.close();
    indexFile.close();
    
    // 释放缓冲区
    delete[] sortedBuffer;
    
    // 用排序后的文件替换原文件
    try {
        fs::remove(filePath);
        fs::rename(sortedFilePath, filePath);
    } catch (const std::exception& e) {
        std::cerr << "替换文件失败: " << e.what() << std::endl;
        return false;
    }
    
    std::cout << "分块构建范围索引完成，共 " << rangeIndex_.size() << " 个范围" << std::endl;
    return true;
}

bool FileCache::buildRangeIndexWithSmallChunks(std::ifstream& dataFile, size_t fileSize, size_t entryCount) {
    std::cout << "使用小块方式构建范围索引..." << std::endl;
    
    // 重置文件位置
    dataFile.seekg(0, std::ios::beg);
    
    // 创建排序后的数据文件
    std::string filePath = dataFiles_[currentLevel_];
    std::string sortedFilePath = filePath + ".sorted";
    std::ofstream sortedFile(sortedFilePath, std::ios::binary | std::ios::out);
    if (!sortedFile) {
        std::cerr << "无法创建排序后的数据文件: " << sortedFilePath << std::endl;
        return false;
    }
    
    // 创建索引文件
    std::string indexPath = getIndexFilePath(currentLevel_);
    std::ofstream indexFile(indexPath, std::ios::binary | std::ios::out);
    if (!indexFile) {
        std::cerr << "无法创建索引文件: " << indexPath << std::endl;
        return false;
    }
    
    // 使用更小的块大小
    constexpr size_t SMALL_CHUNK_SIZE = 10000;
    const size_t totalChunks = (entryCount + SMALL_CHUNK_SIZE - 1) / SMALL_CHUNK_SIZE;
    
    std::cout << "总条目数: " << entryCount << ", 分为 " << totalChunks << " 个小块" << std::endl;
    
    // 创建最小的范围索引
    RangeEntry currentRange{
        std::numeric_limits<Address>::max(),  // 初始最小值
        0,                                  // 初始最大值
        0,                                  // 文件偏移（将在处理第一个块时设置）
        0                                   // 条目计数
    };
    
    bool hasAnyValidData = false;
    int64_t currentFilePos = 0;
    
    // 处理每个块
    for (size_t chunkIndex = 0; chunkIndex < totalChunks; ++chunkIndex) {
        // 计算当前块的大小
        const size_t startEntry = chunkIndex * SMALL_CHUNK_SIZE;
        const size_t chunkEntryCount = std::min(SMALL_CHUNK_SIZE, entryCount - startEntry);
        
        if (chunkIndex % 100 == 0) {
            std::cout << "处理小块 " << (chunkIndex + 1) << "/" << totalChunks << std::endl;
        }
        
        // 定位到块的起始位置
        const int64_t chunkOffset = startEntry * sizeof(PointerCacheEntry);
        dataFile.seekg(chunkOffset);
        if (dataFile.fail()) {
            std::cerr << "跳转到小块位置失败: " << chunkOffset << std::endl;
            dataFile.clear();
            continue;
        }
        
        // 读取块数据
        std::vector<PointerCacheEntry> chunkEntries(chunkEntryCount);
        dataFile.read(reinterpret_cast<char*>(chunkEntries.data()), 
                     chunkEntryCount * sizeof(PointerCacheEntry));
        
        if (dataFile.fail()) {
            std::cerr << "读取小块数据失败" << std::endl;
            dataFile.clear();
            continue;
        }
        
        // 如果这是第一个有效块，设置范围的文件偏移
        if (!hasAnyValidData && !chunkEntries.empty()) {
            currentRange.fileOffset = currentFilePos;
            hasAnyValidData = true;
        }
        
        // 写入数据并更新范围
        for (const auto& entry : chunkEntries) {
            // 更新范围值
            currentRange.startValue = std::min(currentRange.startValue, entry.value);
            currentRange.endValue = std::max(currentRange.endValue, entry.value);
            
            // 写入数据
            sortedFile.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
            if (sortedFile.fail()) {
                std::cerr << "写入排序后的文件失败" << std::endl;
                break;
            }
            
            // 更新条目计数
            currentRange.entryCount++;
        }
        
        // 更新当前文件位置
        currentFilePos += chunkEntryCount * sizeof(PointerCacheEntry);
    }
    
    // 如果有任何有效数据，写入范围
    if (hasAnyValidData) {
        rangeIndex_.push_back(currentRange);
        indexFile.write(reinterpret_cast<const char*>(&currentRange), sizeof(currentRange));
    } else {
        // 创建空范围
        RangeEntry emptyRange{0, 0, 0, 0};
        rangeIndex_.push_back(emptyRange);
        indexFile.write(reinterpret_cast<const char*>(&emptyRange), sizeof(emptyRange));
    }
    
    // 关闭文件
    sortedFile.close();
    indexFile.close();
    
    // 用排序后的文件替换原文件
    try {
        fs::remove(filePath);
        fs::rename(sortedFilePath, filePath);
    } catch (const std::exception& e) {
        std::cerr << "替换文件失败: " << e.what() << std::endl;
        return false;
    }
    
    std::cout << "小块构建范围索引完成，共 " << rangeIndex_.size() << " 个范围" << std::endl;
    return true;
}

void FileCache::cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        if (fs::exists(cacheDir_)) {
            for (const auto& entry : fs::directory_iterator(cacheDir_)) {
                if (entry.path().filename().string().find("memchainer_") == 0) {
                    fs::remove(entry.path());
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "清理缓存文件失败: " << e.what() << std::endl;
    }
    
    // 清空内部状态
    dataFiles_.clear();
    rangeIndex_.clear();
    currentLevel_ = -1;
    currentLevelPointerCount_ = 0;
    
    std::cout << "缓存文件已清理" << std::endl;
}

std::string FileCache::getTempFilePath(int level) const {
    std::ostringstream oss;
    oss << cacheDir_ << "/memchainer_lvl" << level << "_data.bin";
    return oss.str();
}

std::string FileCache::getIndexFilePath(int level) const {
    std::ostringstream oss;
    oss << cacheDir_ << "/memchainer_lvl" << level << "_index.bin";
    return oss.str();
}

bool FileCache::loadIndex(int level) {
    // 清空现有索引
    rangeIndex_.clear();
    
    // 读取索引文件
    std::string indexPath = getIndexFilePath(level);
    std::ifstream indexFile(indexPath, std::ios::binary | std::ios::in);
    if (!indexFile) {
        std::cerr << "无法打开索引文件: " << indexPath << std::endl;
        return false;
    }
    
    // 读取所有范围条目
    RangeEntry entry;
    while (indexFile.read(reinterpret_cast<char*>(&entry), sizeof(entry))) {
        rangeIndex_.push_back(entry);
    }
    
    indexFile.close();
    
    std::cout << "加载范围索引完成，共 " << rangeIndex_.size() << " 个范围" << std::endl;
    return true;
}

size_t FileCache::getCurrentLevelPointerCount() const {
    return currentLevelPointerCount_;
}

} // namespace memchainer 