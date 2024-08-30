cmake_minimum_required(VERSION 3.10)
project(RealTimeDataTester)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(${CMAKE_SOURCE_DIR}/../../third_parts/ib_tws/include)
set(TWS_LIB "${CMAKE_SOURCE_DIR}/../../third_parts/ib_tws/lib/libib_tws.dylib")
set(BID_LIB "${CMAKE_SOURCE_DIR}/../../third_parts/IntelRDFPMathLib20U2/lib/libbid.a")

# Find Boost libraries
find_package(Boost REQUIRED COMPONENTS system filesystem)

# Verify Boost presence
if(Boost_FOUND)
    message(STATUS "Boost found: ${Boost_VERSION}")
    message(STATUS "Boost include dirs: ${Boost_INCLUDE_DIRS}")
    message(STATUS "Boost libraries: ${Boost_LIBRARIES}")
else()
    message(FATAL_ERROR "Boost not found!")
endif()

# Specify the architecture for M1
if (APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
    message(STATUS "Building for Apple Silicon (arm64)")
    set(CMAKE_OSX_ARCHITECTURES "arm64")
endif()

# Add executable target for the tester
add_executable(RealTimeDataTester 
    RealTimeDataTester.cpp
)

# Link libraries to the executable
target_link_libraries(RealTimeDataTester
    ${TWS_LIB}               # Link TWS API library
    ${Boost_LIBRARIES}       # Link Boost libraries
    ${BID_LIB}               # Link IntelRDFPMathLib
    pthread
)

# Set output directory
set_target_properties(RealTimeDataTester PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# Update RPATH settings
if (APPLE)
    set_target_properties(RealTimeDataTester PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "@executable_path;@executable_path/../../third_parts/ib_tws/lib"
    )
endif()

# Add a custom command to copy the dylib to the output directory
add_custom_command(TARGET RealTimeDataTester POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${TWS_LIB}
    ${CMAKE_SOURCE_DIR}/libib_tws.dylib
)
