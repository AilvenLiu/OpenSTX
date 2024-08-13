# 设置最低 CMake 版本要求
cmake_minimum_required(VERSION 3.8)

# 设置项目名称
project(TestTimescaleDB)

# 设置 C++ 标准为 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 添加包含目录 (包括第三方库的头文件)
include_directories(
    ${PROJECT_SOURCE_DIR}/../../third_parts/libpqxx/include
    ${PROJECT_SOURCE_DIR}/../../src
)

# 添加源文件
set(SOURCE_FILES
    TestTimescaleDB.cpp
)

# 查找 libpqxx 库
find_library(PQXX_LIB pqxx HINTS ${PROJECT_SOURCE_DIR}/../../third_parts/libpqxx/lib)

# 如果找不到 libpqxx 库，则给出错误信息
if (NOT PQXX_LIB)
    message(FATAL_ERROR "libpqxx library not found")
endif()

# 查找 PostgreSQL 的安装路径，并包含其库和头文件
find_package(PostgreSQL REQUIRED)

# 创建可执行文件
add_executable(TestTimescaleDB ${SOURCE_FILES})

# 链接 libpqxx 和 libpq 库
target_link_libraries(TestTimescaleDB ${PQXX_LIB} PostgreSQL::PostgreSQL)

# 设置可执行文件的输出目录
set_target_properties(TestTimescaleDB PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
)