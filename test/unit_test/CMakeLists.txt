# 设置最低 CMake 版本要求
cmake_minimum_required(VERSION 3.10)

# 设置项目名称
project(TEST_TimescaleDB)

# 设置 C++ 标准为 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 添加包含目录 (包括第三方库的头文件和项目的头文件)
include_directories(
    ${PROJECT_SOURCE_DIR}/../../third_parts/libpqxx/include  # libpqxx 头文件路径
    ${PROJECT_SOURCE_DIR}/../../include                      # 项目头文件路径
)

# 添加源文件
set(SOURCE_FILES
    main.cpp
    TEST_TimescaleDB.hpp
    ${PROJECT_SOURCE_DIR}/../../src/database/TimescaleDB.cpp  # 修正后的 TimescaleDB 源文件路径
    ${PROJECT_SOURCE_DIR}/../../src/logger/Logger.cpp        # 包含 Logger 源文件路径
)

# 查找 libpqxx 库
find_library(PQXX_LIB pqxx HINTS ${PROJECT_SOURCE_DIR}/../../third_parts/libpqxx/lib)

# 如果找不到 libpqxx 库，则给出错误信息
if (NOT PQXX_LIB)
    message(FATAL_ERROR "libpqxx library not found")
endif()

# 查找 PostgreSQL 的安装路径，并包含其库和头文件
find_package(PostgreSQL REQUIRED)

# 使用 FetchContent 获取 GoogleTest
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/release-1.11.0.zip
)
FetchContent_MakeAvailable(googletest)

# 创建可执行文件
add_executable(TEST_TimescaleDB ${SOURCE_FILES})

# 链接 libpqxx 和 libpq 库
target_link_libraries(TEST_TimescaleDB
    PostgreSQL::PostgreSQL
    gtest
    gtest_main
    ${PQXX_LIB}
)

# 设置可执行文件的输出目录
set_target_properties(TEST_TimescaleDB PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
)