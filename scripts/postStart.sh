#!/bin/bash

echo "Running fpp-pulsemesh PostStart Script"

if [ -f "/.dockerenv" ]; then
    echo "Running in docker, skipping..."
else
    sudo systemctl restart pulsemesh-connector.service
fi



