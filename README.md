# MemoryChainer

MemoryChainer 是一个用于指针链扫描的工具

## 功能特点

- 支持多层级指针链扫描
- 自动识别静态指针和动态指针
- 支持内存区域过滤
- 提供详细的指针链格式化输出
- 支持文件保存和加载


## 主要组件

### 1. 内存访问 (Memory Access)
- 提供统一的内存访问接口
- 支持进程内存读取
- 支持内存区域管理

### 2. 指针扫描器 (Pointer Scanner)
- 多层级指针链扫描
- 静态指针识别
- 内存区域过滤
- 结果优化和压缩

### 3. 格式化输出 (Formatter)
- 支持控制台和文件输出
- 可配置的输出格式（十六进制/十进制）
- 详细的指针链信息展示

## 使用方法

### 基本使用

```cpp
#include "scanner/scanner.h"

// 创建扫描器实例
auto scanner = std::make_shared<memchainer::PointerScanner>();

// 初始化扫描器
scanner->initialize(memAccess, memMap);

// 设置扫描选项
memchainer::PointerScanner::ScanOptions options;
options.maxDepth = 10;
options.maxOffset = 500;

// 执行扫描
auto chain = scanner->scanPointerChain(targetAddress, options);

// 格式化输出
// memchainer::PointerFormatter formatter;
// formatter.setFormat("both");
// formatter.formatToConsole(chain);
```



## TODO 性能优化

- 使用对象池减少内存分配
- 支持数据压缩
- 优化数据结构
- 内存缓存优化
