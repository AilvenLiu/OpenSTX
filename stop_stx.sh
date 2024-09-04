#!/bin/bash

# Get the directory of the script
PROJECT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Path to the OpenSTX executable
EXECUTABLE="$PROJECT_DIR/bin/OpenSTX"

if [ ! -x "$EXECUTABLE" ]; then
    echo "Error: OpenSTX executable not found or not executable at $EXECUTABLE"
    exit 1
fi

echo "Using OpenSTX executable: $EXECUTABLE"

# Kill OpenSTX process
pkill -f "$EXECUTABLE"

# Kill the monitoring script (run_stx.sh)
pkill -f "run_stx.sh"

echo "OpenSTX and monitoring script stopped."