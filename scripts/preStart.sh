#!/bin/bash

echo "Running fpp-pulsemesh PreStart Script"

sudo apt update -o Dir::Etc::sourcelist="/etc/apt/sources.list.d/pulsemsh.list" -o Dir::Etc::sourceparts="-" -o APT::Get::List-Cleanup="0"
sudo apt-get install --only-upgrade -y pulsemesh-connector
sudo systemctl enable pulsemesh-connector.service