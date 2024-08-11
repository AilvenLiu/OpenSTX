cmake_minimum_required(VERSION 3.25)
project(APILibrary)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)

# Set include directories for API headers
include_directories(${PROJECT_SOURCE_DIR}/include)

# Add API source files
file(GLOB API_SOURCES "${PROJECT_SOURCE_DIR}/src/*.cpp")

# Specify the output directory for the dynamic library
set(LIBRARY_OUTPUT_PATH "${PROJECT_SOURCE_DIR}/lib")

# Create a shared library (dynamic library)
add_library(ib_tws SHARED ${API_SOURCES})

# Find Boost libraries
find_package(Boost REQUIRED COMPONENTS system filesystem)

# Find OpenSSL libraries
find_package(OpenSSL REQUIRED)

# Verify Boost presence
if(Boost_FOUND)
    message(STATUS "Boost found: ${Boost_VERSION}")
    message(STATUS "Boost include dirs: ${Boost_INCLUDE_DIRS}")
    message(STATUS "Boost libraries: ${Boost_LIBRARIES}")
else()
    message(FATAL_ERROR "Boost not found!")
endif()

# Verify OpenSSL presence
if(OpenSSL_FOUND)
    message(STATUS "OpenSSL found!")
    message(STATUS "OpenSSL include dirs: ${OPENSSL_INCLUDE_DIR}")
    message(STATUS "OpenSSL libraries: ${OPENSSL_LIBRARIES}")
else()
    message(FATAL_ERROR "OpenSSL not found!")
endif()

# Specify the architecture for M1
if (APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
    message(STATUS "Building for Apple Silicon (arm64)")
    set(CMAKE_OSX_ARCHITECTURES "arm64")
endif()

# Link Boost and OpenSSL libraries
target_link_libraries(ib_tws 
    ${Boost_LIBRARIES} 
    ${OPENSSL_LIBRARIES}
)

# Ensure symbol visibility settings
set_target_properties(ib_tws PROPERTIES
    CXX_VISIBILITY_PRESET default # Change to default for visibility
    VISIBILITY_INLINES_HIDDEN 0   # Disable inlines visibility hidden
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