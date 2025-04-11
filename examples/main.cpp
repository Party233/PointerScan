#include "memory/mem_access.h"
#include "memory/mem_map.h"
#include "scanner/scanner.h"
#include "scanner/formatter.h"
#include "common/cmd_parser.h"
#include <iostream>
#include <memory>
#include <vector>
#include <cstdlib>
#include <cstring>

using namespace memchainer;

int main(int argc, char *argv[])
{
    // 创建命令行解析器
    CommandLineParser parser("MemoryChainer", "高性能内存指针链分析工具");

    // 添加命令行选项
    parser.addOption({'p', "process", "目标进程名称或PID", true, true});
    parser.addOption({'a', "address", "目标地址(16进制，不带0x前缀)", true, false});
    parser.addOption({'d', "depth", "最大搜索深度", true, false, "10"});
    parser.addOption({'o', "offset", "最大偏移量", true, false, "500"});
    parser.addOption({'t', "threads", "线程数量", true, false, "4"});
    parser.addOption({'l', "limit", "结果限制数量", true, false, "0"});
    parser.addOption({'f', "file", "输出文件名", true, false, "pointer_chains.txt"});
    parser.addOption({'v', "verbose", "详细输出模式", false, false});
    parser.addOption({'h', "help", "显示帮助信息", false, false});
    parser.addOption({'c', "cache-dir", "缓存文件目录", true, false, ""});
    parser.addOption({'b', "batch-size", "扫描批次大小", true, false, "10000"});
    parser.addOption({'s', "smart-filter", "使用智能内存区域过滤", false, false});

    // 设置用法说明
    parser.setUsage("[选项] -p <进程名/PID> [-a <地址>]");

    // 解析命令行参数
    if (!parser.parse(argc, argv))
    {
        std::cerr << "错误: " << parser.getErrorMessage() << std::endl;
        parser.showHelp();
        return 1;
    }

    // 如果请求帮助，显示帮助信息并退出
    if (parser.hasOption("help"))
    {
        parser.showHelp();
        return 0;
    }

    // 创建内存访问对象
    auto memAccess = std::make_shared<AndroidMemoryAccess>();

    // 获取目标进程
    std::string targetProcess = parser.getOptionValue("process");
    ProcessId targetPid = -1;

    // 尝试解析进程ID
    try
    {
        targetPid = std::stoi(targetProcess);
    }
    catch (...)
    {
        // 不是数字，认为是进程名
        targetPid = -1;
    }

    // 设置目标进程
    bool success = false;
    if (targetPid > 0)
    {
        success = memAccess->setTargetProcess(targetPid);
    }
    else
    {
        success = memAccess->setTargetProcess(targetProcess);
    }

    if (!success)
    {
        std::cerr << "无法找到目标进程: " << targetProcess << std::endl;
        return 1;
    }

    bool verboseMode = parser.getBoolOption("verbose");
    //if (verboseMode)
    {
        std::cout << "目标进程ID: " << memAccess->getTargetProcessId() << std::endl;
    }

    // 创建并加载内存映射
    auto memMap = std::make_shared<MemoryMap>();
    if (!memMap->loadMemoryMap(memAccess->getTargetProcessId()))
    {
        std::cerr << "无法加载进程内存映射" << std::endl;
        return 1;
    }

    // 设置要扫描的内存区域类型
    memMap->setRegionFilter(
        Anonymous |
        C_alloc |
        C_bss |
        C_data);
    
    // 加载模块信息
    memMap->parseProcessModule();

    // 创建扫描器
    auto scanner = std::make_shared<PointerScanner>();
    if (!scanner->initialize(memAccess, memMap))
    {
        std::cerr << "初始化扫描器失败" << std::endl;
        return 1;
    }

    // 设置扫描选项
    PointerScanner::ScanOptions options;
    options.maxDepth = parser.getIntOption("depth", 10);
    options.maxOffset = parser.getIntOption("offset", 500);
    options.threadCount = parser.getIntOption("threads", 4);

    int limit = parser.getIntOption("limit", 0);
    if (limit > 0)
    {
        options.limitResults = true;
        options.resultLimit = limit;
    }

    // 显示扫描选项

    std::cout << "搜索深度: " << options.maxDepth << std::endl;
    std::cout << "最大偏移量: " << options.maxOffset << std::endl;
    std::cout << "线程数量: " << options.threadCount << std::endl;
    if (options.limitResults)
    {
        std::cout << "结果限制数量: " << options.resultLimit << std::endl;
    }

    // 设置目标地址
    std::vector<Address> targetAddresses;

    // 如果指定了目标地址，使用它
    if (parser.hasOption("address"))
    {
        try
        {
            Address addr = std::stoull(parser.getOptionValue("address"), nullptr, 16);
            targetAddresses.push_back(addr);
            // if (verboseMode) {
            std::cout << "目标地址: 0x" << std::hex << addr << std::dec << std::endl;
            //}
        }
        catch (...)
        {
            std::cerr << "无效的目标地址: " << parser.getOptionValue("address") << std::endl;
            return 1;
        }
    }
    else
    {
        // 提示用户输入目标地址
        std::cout << "请输入目标地址(十六进制，不带0x前缀): ";
        std::string addrInput;
        std::cin >> addrInput;

        try
        {
            Address addr = std::stoull(addrInput, nullptr, 16);
            targetAddresses.push_back(addr);
            if (verboseMode)
            {
                std::cout << "目标地址: 0x" << std::hex << addr << std::dec << std::endl;
            }
        }
        catch (...)
        {
            std::cerr << "无效的目标地址: " << addrInput << std::endl;
            return 1;
        }
    }

    // 设置进度回调
    auto progressCallback = [verboseMode](uint32_t level, uint32_t totalLevels, float progress)
    {
       // if (verboseMode)
        {
            std::cout << "扫描层级 " << level << "/" << totalLevels
                      << " - 进度: " << (progress * 100.0f) << "%" << std::endl;
        }
    };

    std::cout << "开始扫描潜在指针..." << std::endl;
    scanner->findPointers(0, 0);
    //exit(0);
    // 执行扫描
    auto result = scanner->scanPointerChain(targetAddresses[0], options, progressCallback);

    if (!result || result->isEmpty())
    {
        std::cerr << "未找到有效的指针链" << std::endl;
        return 1;
    }

    //std::cout << "找到 " << result->getChainCount() << " 条指针链" << std::endl;

    // // 保存结果到文件
    // std::string outputFile = parser.getOptionValue("file", "pointer_chains.txt");

    // // 格式化结果
    // PointerFormatter formatter;
    // formatter.formatToTextFile(result, outputFile);
    // std::cout << "结果已保存到: " << outputFile << std::endl;

    // // 显示前10条链
    // formatter.formatToConsole(result, 10);

    // // 如果指定了缓存目录
    // if (parser.hasOption("cache-dir")) {
    //     std::string cacheDir = parser.getOptionValue("cache-dir");
    //     scanner->setCachePath(cacheDir);
    //     if (verboseMode) {
    //         std::cout << "文件缓存目录设置为: " << cacheDir << std::endl;
    //     }
    // }

    // // 使用智能过滤
    // if (parser.hasOption("smart-filter")) {
    //     memMap->applySmartFilter();
    //     if (verboseMode) {
    //         std::cout << "启用智能内存区域过滤" << std::endl;
    //     }
    // }

    return 0;
}
