#!/bin/bash

# Define project and build directories
PROJECT_DIR=$(pwd)
BUILD_DIR="$PROJECT_DIR/build"
BIN_DIR="$PROJECT_DIR/bin"
LIB_DIR="$PROJECT_DIR/lib"

echo "Current Direction: $PROJECT_DIR"

# Remove old build directory if it exists
if [ -d "$BUILD_DIR" ]; then
    echo "Removing old build directory..."
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
fi

if [ -d "$BIN_DIR" ]; then
    echo "Removing old  directory..."
    rm -rf "$BIN_DIR"
    mkdir -p "$BIN_DIR"
fi

if [ ! -d "$LIB_DIR" ]; then
    echo "[ERROR] lib path not exist: $LIB_DIR"
    mkdir -p $LIB_DIR
    exit 1
fi

if [ ! -f "$LIB_DIR/libbid.a" ]; then
    echo "[ERROR] lib file not exist: $LIB_DIR/libbid.a"
    echo "go to third_parts/IntelRDFPMathLib20U2 and run make.sh"
    exit 1
fi

cd "$BUILD_DIR"
echo "Running CMake..."
cmake ..
echo "Compiling the project..."
make -j8

# Check if executable was built successfully
EXECUTABLE_FILE="$BIN_DIR/OpenSTX"
if [ -f "$EXECUTABLE_FILE" ]; then
    echo "Executable built successfully: $EXECUTABLE_FILE"
else
    echo "Executable not found. Build may have failed: $EXECUTABLE_FILE"
fi

# Check if any libraries were built successfully
LIB_FILE_PATTERN="$LIB_DIR/*.dylib"
LIB_FILES=($LIB_FILE_PATTERN)
if [ ${#LIB_FILES[@]} -gt 0 ]; then
    echo "Dynamic libraries built successfully:"
    for LIB_FILE in "${LIB_FILES[@]}"; do
        echo "  - $LIB_FILE"
    done
else
    echo "No dynamic libraries found. Build may have failed."
fi
