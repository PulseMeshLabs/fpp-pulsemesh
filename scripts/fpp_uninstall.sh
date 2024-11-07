#!/bin/bash

echo "Running pulsemesh-connector uninstall script"

if [ -f "/.dockerenv" ]; then
    echo "Running in docker, skipping..."
else
    sudo systemctl --now disable pulsemesh-connector.service
    sudo apt-get purge -y pulsemesh-connector
    sudo rm /etc/apt/sources.list.d/pulsemsh.list

    echo "Removed pulsemesh-connector"
fi
