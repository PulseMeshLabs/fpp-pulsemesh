#!/bin/bash

echo "Running fpp-pulsemesh PreStop Script"

stop_docker() {
    INSTALL_DIR="/home/fpp/media/plugins/fpp-PulseMesh"
    PID_FILE="$INSTALL_DIR/pulsemesh.pid"

    if [ -f "$PID_FILE" ]; then
        PID=$(cat "$PID_FILE")
        if ps -p "$PID" > /dev/null 2>&1; then
            echo "Stopping PulseMesh Connector (PID: $PID)..."
            kill "$PID"
            wait "$PID" 2>/dev/null
            echo "PulseMesh Connector stopped successfully."
            rm -f "$PID_FILE"
        else
            echo "No running PulseMesh Connector process found with PID $PID."
            rm -f "$PID_FILE"
        fi
    else
        echo "PID file not found. Attempting to locate and stop PulseMesh Connector process."

        PID=$(pgrep -f "pulsemesh-connector")
        if [ -n "$PID" ]; then
            echo "Found PulseMesh Connector process (PID: $PID). Stopping..."
            kill "$PID"
            wait "$PID" 2>/dev/null
            echo "PulseMesh Connector stopped successfully."
        else
            echo "PulseMesh Connector is not running."
        fi
    fi
}

if [ -f "/.dockerenv" ]; then
    stop_docker
else
    sudo systemctl stop pulsemesh-connector.service
fi