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

# Function to start OpenSTX
start_openstx() {
    nohup "$EXECUTABLE" > /dev/null 2>&1 &
    echo "OpenSTX started with PID $!"
}

# Main loop to monitor and restart OpenSTX
(
while true; do
    if ! pgrep -f "$EXECUTABLE" > /dev/null; then
        echo "OpenSTX is not running. Starting it..." >> "$PROJECT_DIR/logs/openstx_monitor.log"
        start_openstx
    fi
    sleep 10
done
) &

# Disown the background process so it's not terminated when the terminal closes
disown

echo "Monitoring script started in the background. Use stop_stx.sh to terminate."