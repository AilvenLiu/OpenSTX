# Building `libpqxx` from Source

This document provides instructions for compiling `libpqxx` from source. The build process is designed to place the compiled libraries and header files in a specified project directory, keeping them separate from the system's paths.

## Prerequisites

- CMake (version 3.8 or higher)
- A C++17 compatible compiler (e.g., AppleClang, GCC, or MSVC)
- PostgreSQL development libraries

## Steps to Compile `libpqxx`

### 1. Re-write the `CMakeLists.txt`

To ensure that the compiled libraries and header files are placed in your project directory, update the `CMakeLists.txt` file as follows:

```cmake
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
```

### 2. Write a Build Script (`make.sh`)

Create a `make.sh` script to automate the build and installation process:

```bash
#!/bin/bash

# Define project directory
PROJECT_DIR=$(pwd)
BUILD_DIR="$PROJECT_DIR/build"
LIB_DIR="$PROJECT_DIR/lib"
INCLUDE_DIR="$PROJECT_DIR/include"

echo "Current Directory: $PROJECT_DIR"

# Remove old build directory if it exists
if [ -d "$BUILD_DIR" ]; then
    echo "Removing old build directory..."
    rm -rf "$BUILD_DIR"
fi

# Create build directory
echo "Creating build directory..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Run CMake to generate build files
echo "Running CMake..."
cmake -S .. -B . -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$PROJECT_DIR" \
      -DBUILD_SHARED_LIBS=ON

# Compile the project
echo "Compiling the project..."
make -j8

# Install the project (i.e., move libraries and headers to the specified directories)
echo "Installing the libraries and headers..."
make install

# Check if the dynamic library was built and installed successfully
LIB_FILE="$LIB_DIR/libpqxx.dylib"
if [ -f "$LIB_FILE" ]; then
    echo "Dynamic library built and installed successfully: $LIB_FILE"
else
    echo "Dynamic library not found. Build may have failed."
fi

# Check if the include directory was installed successfully
if [ -d "$INCLUDE_DIR/pqxx" ]; then
    echo "Include files installed successfully in: $INCLUDE_DIR/pqxx"
else
    echo "Include files not found. Build may have failed."
fi
```

### 3. Run the Build Script

After modifying the `CMakeLists.txt` and creating the `make.sh` script, you can compile `libpqxx` by running the following commands:

```bash
cd third_parts/libpqxx
bash make.sh
```

### 4. Output Locations

- **Dynamic Library:** The dynamic library (`libpqxx.dylib`) will be located in `third_parts/libpqxx/lib`.
- **Header Files:** The header files will be located in `third_parts/libpqxx/include/pqxx`.

### 5. Using the Compiled `libpqxx`

In your main project, ensure that you update your `CMakeLists.txt` to include the correct paths:

```cmake
include_directories(${PROJECT_SOURCE_DIR}/third_parts/libpqxx/include)
set(PQXX_LIB "${PROJECT_SOURCE_DIR}/third_parts/libpqxx/lib/libpqxx.dylib")

target_link_libraries(OpenSTX
    ${PQXX_LIB}
)
```

This setup ensures that your project uses the locally compiled `libpqxx` library and headers, keeping the system environment clean and controlled.

## Conclusion

By following these steps, you can compile `libpqxx` from source and use it directly in your project, ensuring compatibility and avoiding system-wide dependencies.