#pragma once

#include "scanner/pointer_chain.h"
#include <string>
#include <memory>

namespace memchainer {

class PointerFormatter {
public:
    PointerFormatter();
    ~PointerFormatter();
    
    // 格式化为控制台输出
    void formatToConsole(const std::shared_ptr<PointerChain>& chain, uint32_t maxChains = 0);
    
    // 格式化为文本文件
    bool formatToTextFile(const std::shared_ptr<PointerChain>& chain, const std::string& filename);
};

} // namespace memchainer
