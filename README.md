# Open Smart Trading eXpert (OpenSTX)

OpenSTX is an open-source project designed for financial data quantitative analysis, utilizing Interactive Brokers as the primary data source. The project focuses on efficiently requesting, reorganizing, storing, and feature engineering financial data (L1 and L2 data for stocks and options). It leverages advanced deep learning models for quantitative analysis, visualization, automated metric generation, and incremental online learning to provide deeper insights into financial markets. OpenSTX is built to run on Apple Arm Silicon and macOS platforms, aiming to assist professionals in computer and statistical sciences in understanding financial markets through data.
This project is on its early stage, should be developed continuously. 

## Project Structure

```plaintext
OpenSTX
├── bin
├── build
├── data
│   ├── analysis_pic
│   ├── daily_data
│   └── original_data
├── documents
├── include
├── src
│   ├── data
│   ├── database
│   └── logger
├── test
│   └── build
└── third_parts
    ├── ib_tws
    └── libpqxx
```

### Directory Breakdown

- **bin/**: Contains the compiled binaries.
- **build/**: Used during the build process to store temporary files.
- **data/**: Houses data files used by the project.
  - **analysis_pic/**: Images and plots generated from data analysis.
  - **daily_data/**: Processed daily data files.
  - **original_data/**: Raw data files.
- **documents/**: Documentation related to the project.
- **include/**: General header files used across different modules in the project.
- **src/**: Contains the source files for the application.
  - **data/**: C++ and Python files for data handling.
  - **database/**: Database handling code, including integration with TimescaleDB.
  - **logger/**: Source files for the logging module.
- **test/**: Contains test files for the application.
- **third_parts/**: External libraries used by the project.
  - **ib_tws/**: Integration with Interactive Brokers TWS API.
  - **libpqxx/**: PostgreSQL C++ client library (libpqxx).

### Key Files

- **CMakeLists.txt**: The root-level CMake configuration file for building the project.
- **make.sh**: A shell script for building the project.
- **LICENSE**: Project licensing information.

## Building Third-Party Libraries

### libpqxx

For detailed instructions on building `libpqxx`, please refer to [third_parts/libpqxx/README.md](third_parts/libpqxx/README.md).

### IB TWS API

For detailed instructions on building the Interactive Brokers TWS API, please refer to [third_parts/ib_tws/README.md](third_parts/ib_tws/README.md).

## Building the Application & Running the Application

### Prerequisites

Before building the project on macOS, ensure you have the following tools installed:

1. **Homebrew**: A package manager for macOS.
   ```bash
   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
   ```

2. **CMake**: Required for building C++ projects.
   ```bash
   brew install cmake
   ```

3. **Boost**: A set of C++ libraries.
   ```bash
   brew install boost
   ```

4. **OpenSSL**: Required for secure connections.
   ```bash
   brew install openssl
   ```

5. **PostgreSQL and TimescaleDB**: For database integration.
   ```bash
   brew install libpq
   brew install postgresql
   brew install timescaledb
   brew services start postgresql
   ```
   You may also use a cloud-based TimescaleDB from Amazon Cloud or AliCloud.

6. **GCC or Clang**: Ensure you have an up-to-date C++ compiler, typically provided with Xcode on macOS.

7. **Python**: Required for running Python scripts.
   ```bash
   brew install python
   ```

### Configuring Environment Variables

Ensure that OpenSSL libraries are correctly linked:

```bash
export OPENSSL_ROOT_DIR=$(brew --prefix openssl)
export OPENSSL_LIBRARIES=$(brew --prefix openssl)/lib
export OPENSSL_INCLUDE_DIR=$(brew --prefix openssl)/include
```

### Building the Application

To build the application:

1. **Install dependencies**:
   ```bash
   pip install -r requirements.txt
   ```

2. **Create a build directory and navigate into it**:
   ```bash
   mkdir -p build && cd build
   ```

3. **Run CMake to configure the project**:
   ```bash
   cmake ..
   ```

4. **Build the application**:
   ```bash
   make -j8
   ```

   The executable will be placed in the `bin/` directory.

### Running the Application

To run the main application:

```bash
./bin/OpenSTX
```

### Python Scripts

Python scripts for data fetching and analysis are located in `src/data/`. You can run them directly using Python:

```bash
python src/data/get_data.py
python src/data/get_realtime_data.py
```

## Module Overview

### API Module (`third_parts/ib_tws/`)

- **Purpose**: Provides integration with Interactive Brokers TWS API.
- **Build**: Outputs a dynamic library (`libib_tws.dylib`).
- **Dependencies**: Requires modifications to `Decimal.h` on macOS for compatibility with Apple Silicon. Detailed instructions can be found in the [README.md](third_parts/ib_tws/README.md).

### Data Module (`src/data/`)

- **Purpose**: Handles data fetching, processing, and storage.
- **Technology**: Uses C++ for core data handling and Python for flexible data analysis.

### Logger Module (`src/logger/`)

- **Purpose**: Provides logging facilities.
- **Technology**: Implemented in C++ for performance.

### TimescaleDB Integration (`src/database/TimescaleDB.cpp`)

- **Purpose**: Provides time-series data storage and query capabilities using TimescaleDB.
- **Technology**: Uses `libpqxx` for PostgreSQL and TimescaleDB interaction.
- **Dependencies**: Requires TimescaleDB and PostgreSQL to be installed and configured.

### Main Application (`src/main.cpp`)

- **Purpose**: The entry point of the application.
- **Technology**: C++.

## Troubleshooting and Common Errors

### Undefined Symbols for Architecture arm64

If you encounter undefined symbols related to `libpqxx`, ensure:

1. **Correct Compilation of `libpqxx`**: The library should be compiled correctly with proper linkage to your project.
2. **Consistent C++ Standard and Compiler**: Ensure that the same C++ standard and compiler are used across your project and third-party libraries.
3. **Correct Library Paths**: Verify that your `CMakeLists.txt` correctly includes and links the necessary libraries.

### Other Issues

- **Linking Errors**: Ensure all dependencies are correctly installed and linked.
- **Compiler Errors**: Check for missing headers or incompatible C++ standards.

## License

This project is licensed under the terms specified in the `LICENSE` file.

## Contributing

Contributions are welcome! Please fork the repository and submit a pull request with your changes.
