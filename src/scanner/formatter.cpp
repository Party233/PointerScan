#include "scanner/formatter.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace memchainer {

PointerFormatter::PointerFormatter() = default;

PointerFormatter::~PointerFormatter() = default;

void PointerFormatter::formatToConsole(const std::vector<std::list<PointerChainNode>>& chains, size_t maxChains) {
    if (chains.empty()) {
        std::cout << "没有找到有效的指针链" << std::endl;
        return;
    }

    // 计算实际要显示的链数
    size_t displayCount = maxChains > 0 ? std::min(maxChains, chains.size()) : chains.size();
    
    std::cout << "找到 " << displayCount << " 条指针链" << std::endl;
    printSeparator(std::cout);

    // 显示每条链
    for (size_t i = 0; i < displayCount; ++i) {
        std::cout << "链 " << (i + 1) << ":" << std::endl;
        std::cout << "  " << formatChainToSimple(chains[i]) << std::endl;
        printSeparator(std::cout);
    }
}

bool PointerFormatter::formatToTextFile(const std::vector<std::list<PointerChainNode>>& chains, const std::string& filename) {
    if (chains.empty()) {
        return false;
    }

    std::ofstream file(filename);
    if (!file.is_open()) {
        return false;
    }

    file << "指针链总数: " << chains.size() << std::endl;
    printSeparator(file);

    for (size_t i = 0; i < chains.size(); ++i) {
       // file << "链 " << (i + 1) << ":" << std::endl;
        file << formatChainToSimple(chains[i]) << std::endl;
       // printSeparator(file);
    }

    return true;
}

bool PointerFormatter::initOutputFile(const std::string& filename) {
    std::ofstream file(filename, std::ios::trunc);  // 清空文件
    if (!file.is_open()) {
        return false;
    }
    
    //file << "# MemoryChainer 指针链扫描结果" << std::endl;
    file << "# 格式: [模块+偏移] -> [偏移1] -> [偏移2] -> ... -> 目标地址" << std::endl;
    printSeparator(file);
    
    return true;
}

bool PointerFormatter::appendChainToFile(const std::list<PointerChainNode>& chain, const std::string& filename) {
    if (chain.empty()) {
        return false;
    }
    
    std::ofstream file(filename, std::ios::app);  // 追加模式
    if (!file.is_open()) {
        return false;
    }
    
    file << formatChainToSimple(chain) << std::endl;
    
    return true;
}

bool PointerFormatter::appendChainsToFile(const std::vector<std::list<PointerChainNode>>& chains, const std::string& filename) {
    if (chains.empty()) {
        return false;
    }
    
    std::ofstream file(filename, std::ios::app);  // 追加模式
    if (!file.is_open()) {
        return false;
    }
    
    // 批量写入，减少IO操作次数
    // 使用字符串缓冲区进一步优化
    std::stringstream buffer;
    for (const auto& chain : chains) {
        if (!chain.empty()) {
            buffer << formatChainToSimple(chain) << '\n';
        }
    }
    
    // 一次性写入所有内容
    file << buffer.str();
    
    return true;
}

std::string PointerFormatter::formatChain(const std::list<PointerChainNode>& chains) {
    if (chains.empty()) {
        return "空指针链";
    }

    std::stringstream ss;
    auto it = chains.begin();
    
    // 格式化静态头节点
    if (it != chains.end()) {
        ss << formatStaticNode(*it) << std::endl;
        ++it;
    }

    // 格式化其余节点
    for (; it != chains.end(); ++it) {
        ss << formatPointerNode(*it) << std::endl;
    }
        
    return ss.str();
}

std::string PointerFormatter::formatStaticNode(const PointerChainNode& node) {
    std::stringstream ss;
    
    if (format_ == "hex" || format_ == "both") {
        ss << "static head: 0x" << std::hex << std::setw(16) << std::setfill('0') << node.address
           << " value: 0x" << std::setw(16) << std::setfill('0') << node.value
           << " offset: 0x" << std::setw(8) << std::setfill('0') << node.offset;
        
        if (showStaticOffset_ && node.staticOffset->region) {
            ss << " staticOffset: 0x" << std::setw(8) << std::setfill('0') << node.staticOffset->staticOffset
               << " region: " << node.staticOffset->region->name;
        }
        
        if (format_ == "both") {
            ss << " (" << std::dec << node.address << ", " << node.value << ", " << node.offset << ")";
        }
    } else {
        ss << "static head: " << node.address
           << " value: " << node.value
           << " offset: " << node.offset;
        
        if (showStaticOffset_ && node.staticOffset->region) {
            ss << " staticOffset: " << node.staticOffset->staticOffset
               << " region: " << node.staticOffset->region->name;
        }
    }
    
    return ss.str();
}

//极简输出
std::string PointerFormatter::formatChainToSimple(const std::list<PointerChainNode>& chains) {
    if (chains.empty()) {
        return "空指针链";
    }

    std::stringstream ss;
    ss << std::hex;
    auto it = chains.begin();
    // 格式化静态头节点
    ss << it->staticOffset->region->name << ":";
    ss << "+0x" <<  it->staticOffset->staticOffset;
    //ss << "->0x" << it->offset;
    ++it;
    

    // 格式化其余节点
    for (; it != chains.end(); ++it) {
        ss << "->0x" << it->offset;
    }
    ss << std::dec;
    return ss.str();
}



std::string PointerFormatter::formatPointerNode(const PointerChainNode& node) {
    std::stringstream ss;
    
    if (format_ == "hex" || format_ == "both") {
        ss << "address: 0x" << std::hex << std::setw(16) << std::setfill('0') << node.address
           << " -> value: 0x" << std::setw(16) << std::setfill('0') << node.value
           << " offset: 0x" << std::setw(8) << std::setfill('0') << node.offset;
        
        if (format_ == "both") {
            ss << " (" << std::dec << node.address << ", " << node.value << ", " << node.offset << ")";
        }
    } else {
        ss << "address: " << node.address
           << " -> value: " << node.value
           << " offset: " << node.offset;
    }
    
    return ss.str();
}

template<typename Stream>
void PointerFormatter::printSeparator(Stream& stream) const {
    stream << "----------------------------------------" << std::endl;
}

std::string PointerFormatter::formatStaticOffset(const StaticOffset* staticOffset) const {
    std::stringstream ss;
    
    if (format_ == "hex" || format_ == "both") {
        ss << "静态偏移: 0x" << std::hex << std::setw(8) << std::setfill('0') << staticOffset->staticOffset;
        if (format_ == "both") {
            ss << " (" << std::dec << staticOffset->staticOffset << ")";
        }
    } else {
        ss << "静态偏移: " << staticOffset->staticOffset;
    }
    
    if (showDetails_ && staticOffset->region) {
        ss << " " << formatRegion(staticOffset->region);
    }
    
    return ss.str();
}

std::string PointerFormatter::formatRegion(const MemoryRegion* region) const {
    if (!region) return "";
    
    std::stringstream ss;
    ss << "区域: " << region->name;
    
    if (showDetails_) {
        if (format_ == "hex" || format_ == "both") {
            ss << " [0x" << std::hex << std::setw(16) << std::setfill('0') << region->startAddress
               << "-0x" << std::setw(16) << std::setfill('0') << region->endAddress << "]";
            if (format_ == "both") {
                ss << " [" << std::dec << region->startAddress << "-" << region->endAddress << "]";
            }
        } else {
            ss << " [" << region->startAddress << "-" << region->endAddress << "]";
        }
    }
    
    return ss.str();
}

// 显式实例化模板方法
template void PointerFormatter::printSeparator<std::ostream>(std::ostream& stream) const;
template void PointerFormatter::printSeparator<std::ofstream>(std::ofstream& stream) const;

} // namespace memchainer
