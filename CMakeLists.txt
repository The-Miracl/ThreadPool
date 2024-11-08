# CMake最低版本要求
cmake_minimum_required(VERSION 3.10)

# 项目名称
project(ThreadPoolProject)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 添加可执行文件
add_executable(ThreadPoolProject test.cpp threadpool.cpp)

# 如果你的threadpool.h或其他头文件在某个子目录中，确保包含这个目录
# include_directories(include)