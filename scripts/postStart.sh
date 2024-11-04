#!/bin/bash

echo "Running fpp-pulsemesh PostStart Script"

if [ -f "/.dockerenv" ]; then
    INSTALL_DIR="/home/fpp/media/plugins/fpp-PulseMesh"
    EXECUTABLE_NAME="pulsemesh-connector"

    # Start the executable in the background
    echo "Starting PulseMesh Connector..."
    nohup "$INSTALL_DIR/$EXECUTABLE_NAME" > "/home/fpp/media/logs/pulsemesh-connector.log" 2>&1 &
    echo $! > "$INSTALL_DIR/pulsemesh.pid"
else
    sudo systemctl restart pulsemesh-connector.service
fi



