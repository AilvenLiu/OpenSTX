cmake_minimum_required(VERSION 3.10)
project(OpenSTX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include directories for project headers and API headers
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/third_parts/ib_tws/include)
include_directories(${PROJECT_SOURCE_DIR}/third_parts/libpqxx/include)

# Add source files for the main project
file(GLOB_RECURSE PROJECT_SOURCES 
    "${PROJECT_SOURCE_DIR}/src/logger/Logger.cpp"
    "${PROJECT_SOURCE_DIR}/src/database/TimescaleDB.cpp"
    "${PROJECT_SOURCE_DIR}/src/data/IBClient.cpp"
    "${PROJECT_SOURCE_DIR}/src/data/RealTimeData.cpp"
    "${PROJECT_SOURCE_DIR}/src/data/DailyDataFetcher.cpp"
    "${PROJECT_SOURCE_DIR}/src/main.cpp"
)

# Specify the output directories for binaries and libraries
set(EXECUTABLE_OUTPUT_PATH "${PROJECT_SOURCE_DIR}/bin")
set(LIBRARY_OUTPUT_PATH "${PROJECT_SOURCE_DIR}/lib")
link_directories(${LIBRARY_OUTPUT_PATH})

# Set the path to the TWS API dynamic library
set(TWS_LIB "${PROJECT_SOURCE_DIR}/third_parts/ib_tws/lib/libib_tws.dylib")
set(PQXX_LIB "${PROJECT_SOURCE_DIR}/third_parts/libpqxx/lib/libpqxx.dylib")

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

# Add executable target for the project
add_executable(OpenSTX ${PROJECT_SOURCES})

# Link libraries to the executable
target_link_libraries(OpenSTX
    ${TWS_LIB}               # Link TWS API library
    ${Boost_LIBRARIES}       # Link Boost libraries
    ${OPENSSL_LIBRARIES}     # Link OpenSSL libraries
    ${PQXX_LIB}              # Link libpqxx dynamic library
    ${LIBRARY_OUTPUT_PATH}/libbid.a 
)

# Installation rules
install(TARGETS OpenSTX
    RUNTIME DESTINATION bin
)

# Set compiler options for macOS
if (APPLE)
    # Enforce use of RPATH to locate shared libraries on macOS
    set_target_properties(OpenSTX PROPERTIES
        INSTALL_RPATH "@loader_path/../lib"
    )
endif()