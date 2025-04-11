#pragma once

#include "common/types.h"
#include <system_error>
#include <string>
#include <vector>

namespace memchainer {

// 内存访问错误代码
enum class MemError {
    AccessDenied = 1,
    ProcessNotFound,
    InvalidAddress,
    ReadError,
    PageMapError
};

// 统一的内存访问接口
class MemoryAccess {
public:
    // 构造和析构
    MemoryAccess();
    virtual ~MemoryAccess();

    // 禁用拷贝和移动
    MemoryAccess(const MemoryAccess&) = delete;
    MemoryAccess& operator=(const MemoryAccess&) = delete;
    MemoryAccess(MemoryAccess&&) = delete;
    MemoryAccess& operator=(MemoryAccess&&) = delete;

    // 设置目标进程
    bool setTargetProcess(ProcessId pid);
    bool setTargetProcess(const std::string& processName);
    ProcessId getTargetProcessId() const;

    // 内存读取方法
    template<typename T>
    T read(Address address, std::error_code& ec) const;
    
    bool read(Address address, void* buffer, MemorySize size, std::error_code& ec) const;
    
    // 检查地址是否有效
    bool isValidAddress(Address address) const;
    bool isReadableAddress(Address address, MemorySize size) const;
    
    // 检查页面是否存在
    bool isPagePresent(Address address) const;
    
    // 处理页面错误
    bool checkAndHandlePageFault(Address address) const;

protected:
    // 平台相关的内存读取实现
    virtual bool readMemory(Address address, void* buffer, MemorySize size, std::error_code& ec) const = 0;
    virtual bool isPageMapped(Address address) const = 0;

    ProcessId targetPid_;
    int pageFd_;  // 页面映射文件描述符
    mutable size_t pageFailCount_; // 页面错误计数
    mutable size_t readFailCount_; // 读取失败计数
};

// Android平台实现 - 用于访问Android进程内存
class AndroidMemoryAccess : public MemoryAccess {
public:
    AndroidMemoryAccess();
    ~AndroidMemoryAccess() override;
    
    // 获取Root权限信息
    bool hasRootAccess() const { return hasRootAccess_; }
    
    // 设置Su路径
    void setSuPath(const std::string& suPath) { suPath_ = suPath; }

protected:
    bool readMemory(Address address, void* buffer, MemorySize size, std::error_code& ec) const override;
    bool isPageMapped(Address address) const override;

private:
    int memFd_;           // 进程内存文件描述符
    bool hasRootAccess_;  // 是否有Root权限
    std::string suPath_;  // su命令路径
    
    void openMemoryFile();
    void closeMemoryFile();
    bool tryOpenWithRoot();
    bool checkRootAccess();
};

// 通用Linux平台实现
class LinuxMemoryAccess : public MemoryAccess {
public:
    LinuxMemoryAccess();
    ~LinuxMemoryAccess() override;

protected:
    bool readMemory(Address address, void* buffer, MemorySize size, std::error_code& ec) const override;
    bool isPageMapped(Address address) const override;

private:
    int memFd_;  // 进程内存文件描述符
    void openMemoryFile();
    void closeMemoryFile();
};

// 模板方法实现
template<typename T>
T MemoryAccess::read(Address address, std::error_code& ec) const {
    T value{};
    if (read(address, &value, sizeof(T), ec)) {
        return value;
    }
    return T{};
}

// 错误分类实现
const std::error_category& mem_error_category();
std::error_code make_error_code(MemError e);

} // namespace memchainer

// 注册错误码
namespace std {
    template <>
    struct is_error_code_enum<memchainer::MemError> : true_type {};
}
