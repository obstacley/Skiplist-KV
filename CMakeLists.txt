cmake_minimum_required(VERSION 3.10)

# 2. 项目名称及语言
project(SkiplistKV LANGUAGES CXX)

# 3. 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 4. 指定头文件搜索路径
include_directories(include)

# 5. 指定生成目标
add_executable(skiplist_node src/main.cpp)
