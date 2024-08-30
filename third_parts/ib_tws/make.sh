#!/bin/bash

# Define project and build directories
PROJECT_DIR=$(pwd)
BUILD_DIR="$PROJECT_DIR/build"
LIB_DIR="$PROJECT_DIR/lib"

if [ ! -d "$LIB_DIR" ]; then
  echo "[ERROR] path not exist: $LIB_DIR"
  mkdir -p "$LIB_DIR"
  exit 1
fi

if [ ! -f "$LIB_DIR/libbid.a" ]; then
  echo "[ERROR] file not exist: $LIB_DIR/libbid.a, go to third_parts/IntelRDFPMathLib20U2/LIBRARY and run: "
  echo "./RUNOSX (if you are in MacOS)"
  echo "sudo make install -f macos-install.makefile"
  echo "cp gcc011libbid.a ../../ib_tws/lib/libbid.a"
  echo "cp gcc011libbid.a ../../../lib/libbid.a"
  exit 1
fi

if [ -f "$LIB_DIR/libib_tws_dylib" ]; then
    rm -rf "$LIB_DIR/libib_tws.dylib"
fi

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

# Check if library was built successfully
LIB_FILE="$LIB_DIR/libib_tws.dylib"
if [ -f "$LIB_FILE" ]; then
    echo "Dynamic library built successfully: $LIB_FILE"
else
    echo "Dynamic library not found. Build may have failed: $LIB_FILE"
fi
