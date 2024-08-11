# Building the IB_TWS_API Dynamic Library

This document provides detailed instructions on how to build the `ib_tws` dynamic library from source on macOS. It also covers necessary modifications to avoid `libbid` errors when compiling on Apple Silicon.

## 1. Prepare the Source Code

1. **Download and Prepare the Source Code:**
   Navigate to the `third_parts` directory and prepare the source code:
   ```bash
   cd third_parts
   # Download source code from the IB API official website if not already available
   # Keep the source code and rename `cppclient/source` to `ib_tws_api`
   ```

## 2. Modify `Decimal.h` to Avoid `libbid` Errors on macOS

When compiling on macOS, particularly on Apple Silicon (M1 or M2), you may encounter errors related to `libbid` due to missing BID decimal functions. You can modify the `Decimal.h` file to remove the dependency on `libbid` and use standard C++ operations instead.

1. **Locate `Decimal.h`:**
   Find the `Decimal.h` file in the `ib_tws` source directory.

2. **Replace External BID Decimal Operations:**

   **Original `Decimal.h`:**
   ```cpp
   extern "C" Decimal __bid64_add(Decimal, Decimal, unsigned int, unsigned int*);
   extern "C" Decimal __bid64_sub(Decimal, Decimal, unsigned int, unsigned int*);
   extern "C" Decimal __bid64_mul(Decimal, Decimal, unsigned int, unsigned int*);
   extern "C" Decimal __bid64_div(Decimal, Decimal, unsigned int, unsigned int*);
   extern "C" Decimal __bid64_from_string(char*, unsigned int, unsigned int*);
   extern "C" void __bid64_to_string(char*, Decimal, unsigned int*);
   extern "C" double __bid64_to_binary64(Decimal, unsigned int, unsigned int*);
   extern "C" Decimal __binary64_to_bid64(double, unsigned int, unsigned int*);
   ```

   **Modified `Decimal.h`:**
   Replace the above code with the following to use standard C++ operations:
   ```cpp
   #pragma once
   #ifndef TWS_API_CLIENT_DECIMAL_H
   #define TWS_API_CLIENT_DECIMAL_H

   #include <sstream>
   #include <climits>
   #include <string>
   #include <stdexcept>

   typedef unsigned long long Decimal;
   #define UNSET_DECIMAL ULLONG_MAX

   inline Decimal add(Decimal decimal1, Decimal decimal2) {
       return decimal1 + decimal2;
   }

   inline Decimal sub(Decimal decimal1, Decimal decimal2) {
       return decimal1 >= decimal2 ? decimal1 - decimal2 : 0;
   }

   inline Decimal mul(Decimal decimal1, Decimal decimal2) {
       return decimal1 * decimal2;
   }

   inline Decimal div(Decimal decimal1, Decimal decimal2) {
       if (decimal2 == 0) {
           throw std::runtime_error("Division by zero");
       }
       return decimal1 / decimal2;
   }

   inline Decimal stringToDecimal(const std::string& str) {
       try {
           return std::stoull(str);
       } catch (const std::invalid_argument&) {
           throw std::runtime_error("Invalid decimal string");
       } catch (const std::out_of_range&) {
           throw std::runtime_error("Decimal string out of range");
       }
   }

   inline std::string decimalToString(Decimal value) {
       return std::to_string(value);
   }

   inline std::string decimalStringToDisplay(Decimal value) {
       std::string tempStr = decimalToString(value);
       int expPos = tempStr.find('E');
       if (expPos < 0) {
           return tempStr;
       }

       std::string expStr = tempStr.substr(expPos);
       int exp = std::stoi(expStr.substr(1));
       std::string baseStr = tempStr.substr(0, expPos);

       std::ostringstream oss;
       if (exp < 0) {
           oss << "0.";
           for (int i = -1; i > exp; --i) {
               oss << '0';
           }
           oss << baseStr;
       } else {
           oss << baseStr;
           for (int i = 0; i < exp; ++i) {
               oss << '0';
           }
       }

       return oss.str();
   }

   inline double decimalToDouble(Decimal decimal) {
       return static_cast<double>(decimal);
   }

   inline Decimal doubleToDecimal(double d) {
       return static_cast<Decimal>(d);
   }

   #endif // TWS_API_CLIENT_DECIMAL_H
   ```

## 3. Set Up the `CMakeLists.txt` File

Create a `CMakeLists.txt` file in the `ib_tws` directory to configure the build process.

```cmake
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
```

## 4. Write the Build Script (`make.sh`)

Create a `make.sh` script to automate the build process:

```bash
#!/bin/bash

# Define project and build directories
PROJECT_DIR=$(pwd)
BUILD_DIR="$PROJECT_DIR/build"
LIB_DIR="$PROJECT_DIR/lib"

# Remove old build directory if it exists
if [ -d "$BUILD_DIR" ]; then
    rm -rf "$BUILD_DIR"
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Run CMake to generate build files
cmake ..

# Compile the project
make -j8

# Check if the library was built successfully
LIB_FILE="$LIB_DIR/libib_tws.dylib"
if [ -f "$LIB_FILE" ]; then
    echo "Dynamic library built successfully: $LIB_FILE"
else
    echo "Dynamic library not found. Build may have failed: $LIB_FILE"
fi
```

## 5. Build the `ib_tws` Library

Run the following commands to build the `ib_tws` dynamic library:

1. **Navigate to the `ib_tws` directory:**
   ```bash
   cd third_parts/ib_tws
   ```

2. **Run the build script:**
   ```bash
   bash make.sh
   ```

This script will create a build directory, configure the build using CMake, compile the sources, and place the resulting dynamic library in the `lib` directory.

## 6. Using the `ib_tws` Library in Your Project

Ensure that your project's `CMakeLists.txt` includes the correct paths to the `ib_tws` headers and libraries:

```cmake
include_directories(${PROJECT_SOURCE_DIR}/third_parts/ib_tws/include)
set(TWS_LIB "${PROJECT_SOURCE_DIR}/third_parts/ib_tws/lib/libib_tws.dylib")

target_link_libraries(OpenSTX
    ${TWS_LIB}
)
```

This setup ensures that your project uses the locally compiled `ib_tws` library and headers, avoiding potential conflicts with system-wide installations.