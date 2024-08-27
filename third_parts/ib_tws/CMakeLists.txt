cmake_minimum_required(VERSION 3.10)
project(APILibrary)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set include directories for API headers
include_directories(${PROJECT_SOURCE_DIR}/include ${Boost_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR})

# Add API source files
file(GLOB API_SOURCES "${PROJECT_SOURCE_DIR}/src/*.cpp")

# Specify the output directory for the dynamic library
set(LIBRARY_OUTPUT_PATH "${PROJECT_SOURCE_DIR}/lib")
link_directories(LIBRARY_OUTPUT_PATH)

# Create a shared library (dynamic library)
add_library(ib_tws SHARED ${API_SOURCES})

# Find Boost libraries
find_package(Boost REQUIRED COMPONENTS system filesystem)

# Find OpenSSL libraries
find_package(OpenSSL REQUIRED)

# Specify the architecture for M1
if (APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
    message(STATUS "Building for Apple Silicon (arm64)")
    set(CMAKE_OSX_ARCHITECTURES "arm64")
endif()

# Link Boost and OpenSSL libraries
target_link_libraries(ib_tws 
    ${Boost_LIBRARIES} 
    ${OPENSSL_LIBRARIES}
    ${LIBRARY_OUTPUT_PATH}/libbid.a
)

# Output path configuration
set_target_properties(ib_tws PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/lib
    ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/lib
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
)

# Ensure symbol visibility settings
set_target_properties(ib_tws PROPERTIES
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN 0
)

# Installation rules
install(TARGETS ib_tws
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)