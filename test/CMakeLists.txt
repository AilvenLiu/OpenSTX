cmake_minimum_required(VERSION 3.25)
project(pqxx_test)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/../third_parts/libpqxx/include)

# Add executable target for the project
add_executable(pqxx_test main.cpp)

# Link the pqxx dynamic library
target_link_libraries(pqxx_test
    ${PROJECT_SOURCE_DIR}/../third_parts/libpqxx/lib/libpqxx.dylib
    -lc++                    # Ensure linking against libc++
)

# Specify the architecture for Apple Silicon
if (APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
    message(STATUS "Building for Apple Silicon (arm64)")
    set(CMAKE_OSX_ARCHITECTURES "arm64")
endif()
