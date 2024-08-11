cmake_minimum_required(VERSION 3.8)

file(READ VERSION VER_FILE_CONTENT)
string(STRIP ${VER_FILE_CONTENT} VER_FILE_CONTENT)

project(
    libpqxx
    VERSION ${VER_FILE_CONTENT}
    LANGUAGES CXX
)

if(NOT "${CMAKE_CXX_STANDARD}")
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

option(BUILD_DOC "Build documentation" OFF)

if(NOT SKIP_BUILD_TEST)
    option(BUILD_TEST "Build all test cases" OFF)  # 禁用测试构建
endif()

option(BUILD_SHARED_LIBS "Build shared libraries" ON)  # 启用动态库构建

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(config)

add_subdirectory(src)
add_subdirectory(include)

# 设置自定义的安装路径
set(CMAKE_INSTALL_PREFIX "${PROJECT_SOURCE_DIR}/lib")
set(INCLUDE_INSTALL_DIR "${PROJECT_SOURCE_DIR}/include")
set(LIB_INSTALL_DIR "${PROJECT_SOURCE_DIR}/lib")

# 安装库文件和头文件
install(
    TARGETS pqxx
    LIBRARY DESTINATION ${LIB_INSTALL_DIR}   # 动态库安装路径
    ARCHIVE DESTINATION ${LIB_INSTALL_DIR}   # 静态库安装路径
    RUNTIME DESTINATION ${LIB_INSTALL_DIR}   # 可执行文件安装路径
)

install(
    DIRECTORY "${PROJECT_SOURCE_DIR}/include/pqxx"
    DESTINATION ${INCLUDE_INSTALL_DIR}       # 头文件安装路径
)