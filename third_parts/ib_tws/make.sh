#!/bin/bash

# Define project and build directories
PROJECT_DIR=$(pwd)
BUILD_DIR="$PROJECT_DIR/build"
LIB_DIR="$PROJECT_DIR/lib"

# Remove old build directory if it exists
if [ -d "$BUILD_DIR" ]; then
    rm -rf "$BUILD_DIR"
fi

if [ -f "$LIB_DIR/libib_tws_dylib" ]; then
    rm -rf "$LIB_DIR/libib_tws.dylib"
fi

# Create build directory
# mkdir -p "$LIB_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Run CMake to generate build files
cmake ..

# Compile the project
make -j8

# Check if library was built successfully
LIB_FILE="$LIB_DIR/libib_tws.dylib"
if [ -f "$LIB_FILE" ]; then
    echo "Dynamic library built successfully: $LIB_FILE"
else
    echo "Dynamic library not found. Build may have failed: $LIB_FILE"
fi
