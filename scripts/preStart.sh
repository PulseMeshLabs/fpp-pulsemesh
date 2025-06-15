#!/bin/bash

echo "Running fpp-pulsemesh PreStart Script"

SCRIPT_DIR="$(dirname "$0")"
nohup "$SCRIPT_DIR/restart_pulsemesh.sh" > /home/fpp/media/logs/pulsemesh-connector-setup.log 2>&1 &
