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