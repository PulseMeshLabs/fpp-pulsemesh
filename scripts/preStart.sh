#!/bin/bash

echo "Running fpp-pulsemesh PreStart Script"

if [ -f "/.dockerenv" ]; then
    INSTALL_DIR="/home/fpp/media/plugins/fpp-PulseMesh"
    EXECUTABLE_NAME="pulsemesh-connector"

    # Start the executable in the background
    echo "Starting PulseMesh Connector..."
    nohup "$INSTALL_DIR/$EXECUTABLE_NAME" > "/home/fpp/media/logs/pulsemesh-connector.log" 2>&1 &
    echo $! > "$INSTALL_DIR/pulsemesh.pid"
else
    sudo apt update -o Dir::Etc::sourcelist="/etc/apt/sources.list.d/pulsemsh.list" -o Dir::Etc::sourceparts="-" -o APT::Get::List-Cleanup="0"
    sudo apt-get install --only-upgrade -y pulsemesh-connector
    sudo systemctl enable pulsemesh-connector.service
fi

