#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e

# Define necessary directories
SCRIPT_DIR=$(dirname "$0")
PROJECT_ROOT=$(realpath "$SCRIPT_DIR")
LIBRARY_DIR="$PROJECT_ROOT/LIBRARY"
LOCAL_LIB_DIR="$PROJECT_ROOT/lib"
UPPER_LIB_DIR=$(realpath "$PROJECT_ROOT/../../lib")
IBTWS_LIB_DIR=$(realpath "$PROJECT_ROOT/../ib_tws/lib")

remove_libbid() {
    local dir=$1
    local lib_file="$dir/libbid.a"
    if [[ -f "$lib_file" ]]; then
        rm -f "$lib_file" || { echo "Failed to remove $lib_file"; exit 1; }
        echo "Removed existing $lib_file"
    fi
}

# Remove libbid.a files from specified directories if they exist
remove_libbid "$LOCAL_LIB_DIR"
remove_libbid "$UPPER_LIB_DIR"
remove_libbid "$IBTWS_LIB_DIR"

# Ensure lib directories exist
mkdir -p "$LOCAL_LIB_DIR" || { echo "Failed to create local lib directory: $LOCAL_LIB_DIR"; exit 1; }
mkdir -p "$UPPER_LIB_DIR" || { echo "Failed to create upper lib directory: $UPPER_LIB_DIR"; exit 1; }
mkdir -p "$IBTWS_LIB_DIR" || { echo "Failed to create upper lib directory: $IBTWS_LIB_DIR"; exit 1; }

# Step into the LIBRARY directory and run the RUNOSX script
cd "$LIBRARY_DIR" || { echo "Failed to change directory to LIBRARY"; exit 1; }
./RUNOSX

# Install the static library to the local lib directory using the macos-install.makefile
make install -f macos-install.makefile

# Copy the static library to the upper lib directory
cp "$LOCAL_LIB_DIR/libbid.a" "$UPPER_LIB_DIR/libbid.a" || { echo "Failed to copy libbid.a to $UPPER_LIB_DIR"; exit 1; }
cp "$LOCAL_LIB_DIR/libbid.a" "$IBTWS_LIB_DIR/libbid.a" || { echo "Failed to copy libbid.a to $IBTWS_LIB_DIR"; exit 1; }

find "$LIBRARY_DIR" -type f \( -name "*.a" -o -name "*.o" \) -exec rm -f {} \; || { echo "Failed to remove .a and .o files in LIBRARY directory"; exit 1; }

# Success message
echo "Static library successfully installed to $LOCAL_LIB_DIR and copied to $UPPER_LIB_DIR and $IBTWS_LIB_DIR"
echo "All .a and .o files in LIBRARY directory have been removed."