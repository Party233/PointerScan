#include "scanner/formatter.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace memchainer {

PointerFormatter::PointerFormatter() = default;

PointerFormatter::~PointerFormatter() = default;

void PointerFormatter::formatToConsole(const std::shared_ptr<PointerChain>& chain, size_t maxChains) {
    if (!chain || chain->isEmpty()) {
        std::cout << "没有找到有效的指针链" << std::endl;
        return;
    }

    // 计算实际要显示的链数
    size_t displayCount = maxChains > 0 ? std::min(maxChains, chain->getTotalChains()) : chain->getTotalChains();
    
    std::cout << "找到 " << displayCount << " 条指针链" << std::endl;
    std::cout << "----------------------------------------" << std::endl;

    // 显示每条链
    for (size_t i = 0; i < displayCount; ++i) {
        std::cout << "链 " << i + 1 << ":" << std::endl;
        
        const auto& chainNodes = chain->chains_[i];
        for (const auto& node : chainNodes) {
            std::cout << "  " << formatNode(node) << std::endl;
        }
        
        std::cout << "----------------------------------------" << std::endl;
    }
}

bool PointerFormatter::formatToTextFile(const std::shared_ptr<PointerChain>& chain, const std::string& filename) {
    if (!chain || chain->isEmpty()) {
        return false;
    }

    std::ofstream file(filename);
    if (!file.is_open()) {
        return false;
    }

    file << "指针链总数: " << chain->getTotalChains() << std::endl;

    file << "----------------------------------------" << std::endl;

    for (size_t i = 0; i < chain->getTotalChains(); ++i) {
        file << "链 " << i + 1 << ":" << std::endl;
        
        const auto& chainNodes = chain->chains_[i];
        for (const auto& node : chainNodes) {
            file << "  " << formatNode(node) << std::endl;
        }
        
        file << "----------------------------------------" << std::endl;
    }

    return true;
}

std::string PointerFormatter::formatNode(const PointerChainNode& node) const {
    std::stringstream ss;
    
    // 地址
    if (format_ == "hex" || format_ == "both") {
        ss << "地址: 0x" << std::hex << std::setw(16) << std::setfill('0') << node.address;
        if (format_ == "both") {
            ss << " (" << std::dec << node.address << ")";
        }
    } else {
        ss << "地址: " << node.address;
    }
    
    // 值
    if (format_ == "hex" || format_ == "both") {
        ss << " 值: 0x" << std::hex << std::setw(16) << std::setfill('0') << node.value;
        if (format_ == "both") {
            ss << " (" << std::dec << node.value << ")";
        }
    } else {
        ss << " 值: " << node.value;
    }
    
    // 偏移量
    if (format_ == "hex" || format_ == "both") {
        ss << " 偏移: 0x" << std::hex << std::setw(8) << std::setfill('0') << node.offset;
        if (format_ == "both") {
            ss << " (" << std::dec << node.offset << ")";
        }
    } else {
        ss << " 偏移: " << node.offset;
    }
    
    // 静态偏移信息
    if (showStaticOffset_ && node.staticOffset.staticOffset > 0) {
        ss << " " << formatStaticOffset(node.staticOffset);
    }
    
    return ss.str();
}

std::string PointerFormatter::formatStaticOffset(const StaticOffset& staticOffset) const {
    std::stringstream ss;
    
    if (format_ == "hex" || format_ == "both") {
        ss << "静态偏移: 0x" << std::hex << std::setw(8) << std::setfill('0') << staticOffset.staticOffset;
        if (format_ == "both") {
            ss << " (" << std::dec << staticOffset.staticOffset << ")";
        }
    } else {
        ss << "静态偏移: " << staticOffset.staticOffset;
    }
    
    if (showDetails_ && staticOffset.region) {
        ss << " " << formatRegion(staticOffset.region);
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

} // namespace memchainer
