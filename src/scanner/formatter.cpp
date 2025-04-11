#include "scanner/formatter.h"
#include <iostream>
#include <fstream>
#include <iomanip>

namespace memchainer {

PointerFormatter::PointerFormatter() {
}

PointerFormatter::~PointerFormatter() {
}

void PointerFormatter::formatToConsole(const std::shared_ptr<PointerChain>& chain, uint32_t maxChains) {
    if (!chain || chain->isEmpty()) {
        std::cout << "无指针链数据" << std::endl;
        return;
    }
    
    uint32_t chainCount = chain->getChainCount();
    uint32_t levelCount = chain->getLevelCount();
    
    std::cout << "发现 " << chainCount << " 条指针链，" << levelCount << " 层" << std::endl;
    
    // 如果指定了最大链数，则限制输出
    if (maxChains > 0 && maxChains < chainCount) {
        chainCount = maxChains;
    }
    
    // 输出每条链
    for (uint32_t i = 0; i < chainCount; ++i) {
        std::cout << std::endl << "链 #" << (i + 1) << ":" << std::endl;
        
        // 获取基址和偏移量
        Address baseAddress = 0;
        if (!chain->getPointersAtLevel(0).empty()) {
            baseAddress = chain->getPointersAtLevel(0)[0].address;
        }
        
        std::vector<Offset> offsets = chain->getChainOffsets(i);
        
        // 输出基址
        std::cout << "基址: 0x" << std::hex << std::setw(16) << std::setfill('0') << baseAddress << std::dec << std::endl;
        
        // 输出偏移链
        std::cout << "偏移链: ";
        for (size_t j = 0; j < offsets.size(); ++j) {
            std::cout << "0x" << std::hex << offsets[j] << std::dec;
            if (j < offsets.size() - 1) {
                std::cout << " → ";
            }
        }
        std::cout << std::endl;
        
        // 输出最终地址
        Address finalAddress = chain->getPointersAtLevel(levelCount - 1)[i].address;
        std::cout << "最终地址: 0x" << std::hex << std::setw(16) << std::setfill('0') << finalAddress << std::dec << std::endl;
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
    
    uint32_t chainCount = chain->getChainCount();
    uint32_t levelCount = chain->getLevelCount();
    
    file << "# 指针链结果" << std::endl;
    file << "# 总链数: " << chainCount << std::endl;
    file << "# 层数: " << levelCount << std::endl << std::endl;
    
    // 输出每条链
    for (uint32_t i = 0; i < chainCount; ++i) {
        file << "链 #" << (i + 1) << ":" << std::endl;
        
        // 获取基址和偏移量
        Address baseAddress = 0;
        if (!chain->getPointersAtLevel(0).empty()) {
            baseAddress = chain->getPointersAtLevel(0)[0].address;
        }
        
        std::vector<Offset> offsets = chain->getChainOffsets(i);
        
        // 输出基址
        file << "基址: 0x" << std::hex << std::setw(16) << std::setfill('0') << baseAddress << std::dec << std::endl;
        
        // 输出偏移链
        file << "偏移链: ";
        for (size_t j = 0; j < offsets.size(); ++j) {
            file << "0x" << std::hex << offsets[j] << std::dec;
            if (j < offsets.size() - 1) {
                file << " → ";
            }
        }
        file << std::endl;
        
        // 输出最终地址
        Address finalAddress = chain->getPointersAtLevel(levelCount - 1)[i].address;
        file << "最终地址: 0x" << std::hex << std::setw(16) << std::setfill('0') << finalAddress << std::dec << std::endl;
        file << std::endl;
    }
    
    file.close();
    return true;
}

} // namespace memchainer
