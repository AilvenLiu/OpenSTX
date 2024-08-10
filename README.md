# Quant Trading Project

This is a quantitative trading project that leverages C++ for high-performance components and Python for data analysis and visualization. The project is modular, with distinct components for API integration, data handling, logging, and real-time data fetching.

## Project Structure

```plaintext
quant_trading
├── CMakeLists.txt
├── LICENSE
├── api
│   ├── CMakeLists.txt
│   ├── include
│   ├── make.sh
│   └── src
├── data
│   ├── analysis_pic
│   ├── daily_data
│   └── original_data
├── documents
├── include
├── requirements.txt
└── src
    ├── data
    ├── logger
    └── main.cpp
```

### Directory Breakdown

- **CMakeLists.txt**: The root-level CMake configuration file that ties together all sub-projects.

- **LICENSE**: License information for the project.

- **api/**: Contains the source and header files for the Interactive Brokers API integration.

  - **include/**: Header files for the API.
  
  - **src/**: Source files implementing the API functionality.
  
  - **CMakeLists.txt**: CMake configuration for building the API as a dynamic library.
  
  - **make.sh**: Shell script for building the API.

- **data/**: Contains data files used by the project.

  - **analysis_pic/**: Images and plots generated from data analysis.
  
  - **daily_data/**: Processed daily data files.
  
  - **original_data/**: Original raw data files.

- **documents/**: Documentation files related to the project.

- **include/**: General header files used across different modules in the project.

- **requirements.txt**: Lists Python dependencies needed for the project.

- **src/**: Source files for the application.

  - **data/**: Contains C++ and Python files related to data handling.
  
  - **logger/**: Contains source files for the logging module.
  
  - **main.cpp**: Main entry point of the application.

## Building the Project on MacOS

### Prerequisites

Before you can build the project on MacOS, ensure you have the following tools installed:

1. **Homebrew**: A package manager for MacOS. Install it if you haven't already:

   ```bash
   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
   ```

2. **CMake**: Required for building C++ projects.

   ```bash
   brew install cmake
   ```
   Should use c++17 standard.

3. **Boost**: A set of libraries for C++.

   ```bash
   brew install boost
   ```

4. **OpenSSL**: Required for secure connections.

   ```bash
   brew install openssl
   ```
4. **PostgresDB**: Required for secure connections.

   ```bash
   brew install libpqxx
   brew install pkg-config
   brew install postgressql
   brew install timescaledb
   brew services start postgresql
   ```
   
   Or we suggest to use the cloud TimescaleDB from Amazon Cloud or AliCloud.

5. **GCC or Clang**: Ensure you have an up-to-date C++ compiler. The Apple Clang compiler is typically installed with Xcode.

6. **Python**: Ensure Python is installed for running Python scripts.

### Configuring Environment Variables

Ensure that the OpenSSL libraries are correctly linked during the build process. You might need to set the following environment variables:

```bash
export OPENSSL_ROOT_DIR=$(brew --prefix openssl)
export OPENSSL_LIBRARIES=$(brew --prefix openssl)/lib
export OPENSSL_INCLUDE_DIR=$(brew --prefix openssl)/include
```

### Modifying `Decimal.h` to Avoid `libbid` Errors

When compiling on MacOS with Apple Silicon (M1 or M2), you might encounter errors related to `libbid`. This is due to missing BID decimal functions. You can modify the `Decimal.h` file to remove the dependency on `libbid` and use standard C++ operations instead.

Here’s how you can modify `Decimal.h`:

#### Original `Decimal.h`

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

#### Modified `Decimal.h`

Replace external BID decimal operations with standard C++ operations:

```cpp
#pragma once
#ifndef TWS_API_CLIENT_DECIMAL_H
#define TWS_API_CLIENT_DECIMAL_H

#include <sstream>
#include <climits>
#include <string>
#include <stdexcept>

// Decimal type
typedef unsigned long long Decimal;

#define UNSET_DECIMAL ULLONG_MAX

// Implement arithmetic functions using standard C++
inline Decimal add(Decimal decimal1, Decimal decimal2) {
    return decimal1 + decimal2;
}

inline Decimal sub(Decimal decimal1, Decimal decimal2) {
    return decimal1 >= decimal2 ? decimal1 - decimal2 : 0;  // Handle underflow
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

// Conversion functions using standard library
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

### Building the API Dynamic Library

1. **Navigate to the API Directory**:

   ```bash
   cd api
   ```

2. **Run the build script**:

   ```bash
   ./make.sh
   ```

   This script will create a build directory, configure the build using CMake, compile the sources, and place the resulting dynamic library in the `build/lib` directory.

### Building the Application

To build the application executable:

Run `make.sh` or:

1. **Ensure all dependencies are installed.** You can install the Python dependencies using pip:

   ```bash
   pip install -r requirements.txt
   ```

2. **Create a build directory in the root project directory and navigate into it**:

   ```bash
   mkdir -p build && cd build
   ```

3. **Run CMake to configure the project**:

   ```bash
   cmake ..
   ```

4. **Build the application**:

   ```bash
   make
   ```

   The resulting executable will be placed in the `build` directory.

### Running the Application

To run the main application, execute the compiled binary from the build directory:

```bash
./main
```

### Python Scripts

Python scripts for data fetching and analysis are located in `src/data/`. You can run them directly using Python:

```bash
python src/data/get_data.py
python src/data/get_realtime_data.py
```

## Modules Overview

### API Module (`api/`)

- **Purpose**: Integrates with Interactive Brokers TWS API to facilitate trading operations and data fetching.
- **Technology**: C++ with dependencies on Boost and OpenSSL for robust networking and security features.
- **Build**: Outputs a dynamic library (`libapi_library.dylib` or equivalent) used by other parts of the application.

### Data Module (`src/data/`)

- **Purpose**: Handles data fetching, processing, and storage.
- **Technology**: Combines C++ for high-performance operations with Python for flexible data analysis and manipulation.
- **Components**:
  - **C++**: Implements core data handling functionality.
  - **Python**: Provides scripts for real-time data fetching and processing.

### Logger Module (`src/logger/`)

- **Purpose**: Provides logging facilities for the application.
- **Technology**: Implemented in C++ for high-performance logging with minimal overhead.
- **Components**:
  - **Logger.cpp**: Core logging functionality.

### Main Application (`src/main.cpp`)

- **Purpose**: The entry point of the application, orchestrates interactions between different modules.
- **Technology**: C++.

## License

This project is licensed under the terms specified in the `LICENSE` file.

## Contributing

Contributions are welcome! Please fork the repository and submit a pull request with your changes.