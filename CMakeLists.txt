cmake_minimum_required(VERSION 3.10)


# 设置C++标准
set(CMAKE_CXX_STANDARD 17)


##################### ⬇Android设置⬇ #####################
set(CMAKE_SYSTEM_NAME ANDROID) # 设置目标编译平台参数 Android
set(CMAKE_SYSTEM_VERSION 28) # 系统版本
set(ANDROID_PLATFORM 28) # 平台版本
set(ANDROID_ABI arm64-v8a) # 设置目标构架 armeabi-v7a arm64-v8a x86 x86_64
##
## 由于ANDROID_ABI一次编译只能设置一种架构
## 因此该参数在build.sh脚本中单独设置 并且每种架构编译一次
##
set(CMAKE_ANDROID_NDK D:/ndkollvm/android-ndk-r25c.Ollvm/android-ndk-r25c)
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_ANDROID_NDK}/build/cmake/android.toolchain.cmake)


project(MemoryChainer VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_ANDROID_ARCH_ABI arm64-v8a)
set(ANDROID_ABI arm64-v8a)
set(ANDROID_PLATFORM android-26)
set(ANDROID_NDK ${CMAKE_ANDROID_NDK})
##################### ⬆Android设置⬆ #####################

##################### ⬇输出文件重定向⬇ #####################
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY 
    ${CMAKE_SOURCE_DIR}/outputs/
) # 重定向输出产物(可执行文件)
##################### ⬆输出文件重定向⬆ #####################


# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/common
    ${CMAKE_CURRENT_SOURCE_DIR}/include/memory
    ${CMAKE_CURRENT_SOURCE_DIR}/include/scanner

    
    )

# 源文件
file(GLOB_RECURSE SOURCES 
"src/common/*.cpp"
"src/memory/*.cpp"
"src/scanner/*.cpp"
)



# 例子程序
add_executable(scanner_example 
examples/main.cpp
${SOURCES}
)


# add_executable(test_example 
# examples/test.cpp
# )





    