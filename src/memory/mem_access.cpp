#include "memory/mem_access.h"
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <cerrno>
#include <algorithm>
#include <cstdio>
#include <string>
#include <fstream>
#include <iostream>

namespace memchainer {

// 错误类别定义
class MemErrorCategory : public std::error_category {
public:
    const char* name() const noexcept override {
        return "memory_access";
    }
    
    std::string message(int ev) const override {
        switch (static_cast<MemError>(ev)) {
            case MemError::AccessDenied:
                return "拒绝访问内存";
            case MemError::ProcessNotFound:
                return "找不到目标进程";
            case MemError::InvalidAddress:
                return "无效的内存地址";
            case MemError::ReadError:
                return "读取内存失败";
            case MemError::PageMapError:
                return "无法访问页面映射";
            default:
                return "未知内存访问错误";
        }
    }
};

// 全局错误类别实例
const std::error_category& mem_error_category() {
    static MemErrorCategory category;
    return category;
}

// 创建错误代码
std::error_code make_error_code(MemError e) {
    return {static_cast<int>(e), mem_error_category()};
}

// MemoryAccess 基类实现
MemoryAccess::MemoryAccess() 
    : targetPid_(-1), pageFd_(-1), pageFailCount_(0), readFailCount_(0) {
}

MemoryAccess::~MemoryAccess() {
    if (pageFd_ >= 0) {
        close(pageFd_);
        pageFd_ = -1;
    }
}

bool MemoryAccess::setTargetProcess(ProcessId pid) {
    // 验证进程存在
    char procPath[64];
    snprintf(procPath, sizeof(procPath), "/proc/%d", pid);
    
    if (access(procPath, F_OK) != 0) {
        return false;
    }
    
    targetPid_ = pid;
    pageFailCount_ = 0;
    readFailCount_ = 0;
    
    // 关闭旧的页面映射文件描述符
    if (pageFd_ >= 0) {
        close(pageFd_);
        pageFd_ = -1;
    }
    
    // 打开新的页面映射文件
    char pageMapPath[64];
    snprintf(pageMapPath, sizeof(pageMapPath), "/proc/%d/pagemap", pid);
    
    pageFd_ = open(pageMapPath, O_RDONLY);
    
    return targetPid_ > 0;
}

bool MemoryAccess::setTargetProcess(const std::string& processName) {
    DIR* dir = opendir("/proc");
    if (!dir) {
        return false;
    }
    
    struct dirent* entry;
    ProcessId pid = -1;
    
    while ((entry = readdir(dir)) != nullptr) {
        // 检查是否为数字目录（进程 ID）
        if (entry->d_type == DT_DIR && isdigit(entry->d_name[0])) {
            char cmdlinePath[64];
            snprintf(cmdlinePath, sizeof(cmdlinePath), "/proc/%s/cmdline", entry->d_name);
            
            int fd = open(cmdlinePath, O_RDONLY);
            if (fd >= 0) {
                char cmdline[256] = {0};
                ::read(fd, cmdline, sizeof(cmdline) - 1);
                close(fd);
                
                // 提取进程名（第一个空字符之前的部分）
                std::string name = cmdline;
                size_t pos = name.find('\0');
                if (pos != std::string::npos) {
                    name = name.substr(0, pos);
                }
                
                // 提取基本文件名
                pos = name.find_last_of('/');
                if (pos != std::string::npos) {
                    name = name.substr(pos + 1);
                }
                
                // 进行名称匹配 - 支持部分匹配
                if (name.find(processName) != std::string::npos) {
                    pid = static_cast<ProcessId>(std::stoi(entry->d_name));
                    break;
                }
            }
        }
    }
    
    closedir(dir);
    
    if (pid > 0) {
        return setTargetProcess(pid);
    }
    
    return false;
}

ProcessId MemoryAccess::getTargetProcessId() const {
    return targetPid_;
}

bool MemoryAccess::read(Address address, void* buffer, MemorySize size, std::error_code& ec) const {
    // 检查目标进程是否有效
    if (targetPid_ <= 0) {
        ec = make_error_code(MemError::ProcessNotFound);
        return false;
    }
    
    // 简单地址验证
    if (address == 0 || address > 0x7FFFFFFFFFFF) {
        ec = make_error_code(MemError::InvalidAddress);
        return false;
    }
    
    // 清空缓冲区，避免包含无效数据
    memset(buffer, 0, size);
    
    // 调用平台相关的内存读取实现
    if (readMemory(address, buffer, size, ec)) {
        return true;
    }
    
    // 处理读取失败
    //readFailCount_++;
    
    // 尝试检查和处理页面错误后重试
    // if (checkAndHandlePageFault(address)) {
    //     // 重试读取
    //     if (readMemory(address, buffer, size, ec)) {
    //         return true;
    //     }
    // }
    
    return false;
}

bool MemoryAccess::isValidAddress(Address address) const {
    // 简单检查地址是否为0或非常大的值
    if (address == 0 || address > 0x7FFFFFFFFFFF) {
        return false;
    }
    
    // 检查页面是否存在
    return isPagePresent(address);
}

bool MemoryAccess::isReadableAddress(Address address, MemorySize size) const {
    // 检查地址范围是否可读
    const size_t kPageSize = 4096;
    
    // 只检查起始和结束地址的页面
    if (!isPagePresent(address)) {
        return false;
    }
    
    if (size > kPageSize) {
        // 如果区域跨页，还需要检查结束地址
        if (!isPagePresent(address + size - 1)) {
            return false;
        }
    }
    
    return true;
}

bool MemoryAccess::isPagePresent(Address address) const {
    // 检查页面映射文件是否打开
    if (pageFd_ < 0) {
        return false;
    }
    
    // 计算页面索引
    const size_t kPageSize = 4096;
    uint64_t pageIndex = address / kPageSize;
    
    // 查询页面映射信息
    uint64_t pageData = 0;
    off_t offset = pageIndex * sizeof(uint64_t);
    
    if (pread(pageFd_, &pageData, sizeof(pageData), offset) != sizeof(pageData)) {
        return false;
    }
    
    // 检查页面是否存在（第63位）
    // 参考: https://www.kernel.org/doc/Documentation/vm/pagemap.txt
    return (pageData & (1ULL << 63)) != 0;
}

bool MemoryAccess::checkAndHandlePageFault(Address address) const {
    // 页面错误计数增加
    pageFailCount_++;
    
    // Android/Linux上没有直接的方式从用户空间手动触发页面错误处理
    // 这里我们可以尝试进行一次小的读取来触发内核处理缺页
    
    char dummyBuffer[1];
    std::error_code ec;
    
    // 尝试读取一个字节，可能会触发页面错误处理
    if (readMemory(address, dummyBuffer, 1, ec)) {
        // 读取成功，页面现在可能已存在
        return true;
    }
    
    // 如果还是失败，页面可能真的无法访问
    return false;
}

// AndroidMemoryAccess 实现
AndroidMemoryAccess::AndroidMemoryAccess() 
    : MemoryAccess(), memFd_(-1), hasRootAccess_(false), suPath_("su") {
    // 检查是否有Root权限
    hasRootAccess_ = checkRootAccess();
}

AndroidMemoryAccess::~AndroidMemoryAccess() {
    closeMemoryFile();
}

bool AndroidMemoryAccess::checkRootAccess() {
    // 尝试执行su -c id命令，检查是否有root权限
    FILE* pipe = popen("su -c id", "r");
    if (!pipe) {
        return false;
    }
    
    char buffer[128];
    std::string result = "";
    while (!feof(pipe)) {
        if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result += buffer;
        }
    }
    
    int status = pclose(pipe);
    
    // 检查命令是否成功执行并包含uid=0(root)
    return status == 0 && result.find("uid=0") != std::string::npos;
}

void AndroidMemoryAccess::openMemoryFile() {
    closeMemoryFile();
    
    if (targetPid_ > 0) {
        char memPath[64];
        snprintf(memPath, sizeof(memPath), "/proc/%d/mem", targetPid_);
        
        // 尝试直接打开
        memFd_ = open(memPath, O_RDONLY);
        
        // 如果失败且有root权限，尝试使用root权限打开
        if (memFd_ < 0 && hasRootAccess_) {
            if (tryOpenWithRoot()) {
                return;
            }
        }
    }
}

bool AndroidMemoryAccess::tryOpenWithRoot() {
    char command[256];
    char tempFilePath[] = "/data/local/tmp/memaccess_XXXXXX";
    int tempFd = mkstemp(tempFilePath);
    
    if (tempFd < 0) {
        return false;
    }
    
    // 关闭并删除临时文件，我们只需要路径
    close(tempFd);
    unlink(tempFilePath);
    
    // 使用su命令，将目标进程mem文件复制到临时文件
    snprintf(command, sizeof(command), 
            "%s -c \"cat /proc/%d/mem > %s && chmod 666 %s\"", 
            suPath_.c_str(), targetPid_, tempFilePath, tempFilePath);
    
    int result = system(command);
    if (result != 0) {
        return false;
    }
    
    // 打开临时文件
    memFd_ = open(tempFilePath, O_RDONLY);
    
    // 删除临时文件
    unlink(tempFilePath);
    
    return memFd_ >= 0;
}

void AndroidMemoryAccess::closeMemoryFile() {
    if (memFd_ >= 0) {
        close(memFd_);
        memFd_ = -1;
    }
}

bool AndroidMemoryAccess::readMemory(Address address, void* buffer, MemorySize size, std::error_code& ec) const {
    // 确保内存文件已打开
    // if (memFd_ < 0) {
    //     const_cast<AndroidMemoryAccess*>(this)->openMemoryFile();
        
    //     if (memFd_ < 0) {
    //         ec = make_error_code(MemError::AccessDenied);
    //         return false;
    //     }
    // }
    
    // // 尝试从进程内存读取数据
    // ssize_t bytesRead = pread(memFd_, buffer, size, static_cast<off_t>(address));
    
    // if (bytesRead != static_cast<ssize_t>(size)) 
    {
        // 使用进程虚拟内存方法读取失败，尝试使用process_vm_readv (Android 4.4+)
        struct iovec local[1];
        struct iovec remote[1];
        
        local[0].iov_base = buffer;
        local[0].iov_len = size;
        remote[0].iov_base = reinterpret_cast<void*>(address);
        remote[0].iov_len = size;
        
        ssize_t bytesRead = process_vm_readv(targetPid_, local, 1, remote, 1, 0);
        
        if (bytesRead != static_cast<ssize_t>(size)) {
            ec = make_error_code(MemError::ReadError);
            return false;
        }
    }
    
    return true;
}

bool AndroidMemoryAccess::isPageMapped(Address address) const {
    return isPagePresent(address);
}

// LinuxMemoryAccess 实现
LinuxMemoryAccess::LinuxMemoryAccess() : MemoryAccess(), memFd_(-1) {
}

LinuxMemoryAccess::~LinuxMemoryAccess() {
    closeMemoryFile();
}

void LinuxMemoryAccess::openMemoryFile() {
    closeMemoryFile();
    
    if (targetPid_ > 0) {
        char memPath[64];
        snprintf(memPath, sizeof(memPath), "/proc/%d/mem", targetPid_);
        
        memFd_ = open(memPath, O_RDONLY);
    }
}

void LinuxMemoryAccess::closeMemoryFile() {
    if (memFd_ >= 0) {
        close(memFd_);
        memFd_ = -1;
    }
}

bool LinuxMemoryAccess::readMemory(Address address, void* buffer, MemorySize size, std::error_code& ec) const {
    // 确保内存文件已打开
    if (memFd_ < 0) {
        const_cast<LinuxMemoryAccess*>(this)->openMemoryFile();
        
        if (memFd_ < 0) {
            ec = make_error_code(MemError::AccessDenied);
            return false;
        }
    }
    
    // 尝试从进程内存读取数据
    ssize_t bytesRead = pread(memFd_, buffer, size, static_cast<off_t>(address));
    
    if (bytesRead != static_cast<ssize_t>(size)) {
        // 使用进程虚拟内存方法读取失败，尝试使用process_vm_readv
        struct iovec local[1];
        struct iovec remote[1];
        
        local[0].iov_base = buffer;
        local[0].iov_len = size;
        remote[0].iov_base = reinterpret_cast<void*>(address);
        remote[0].iov_len = size;
        
        bytesRead = process_vm_readv(targetPid_, local, 1, remote, 1, 0);
        
        if (bytesRead != static_cast<ssize_t>(size)) {
            ec = make_error_code(MemError::ReadError);
            return false;
        }
    }
    
    return true;
}

bool LinuxMemoryAccess::isPageMapped(Address address) const {
    return isPagePresent(address);
}

} // namespace memchainer
