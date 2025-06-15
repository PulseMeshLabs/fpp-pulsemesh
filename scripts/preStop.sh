#!/bin/bash

echo "Running fpp-pulsemesh PreStop Script"

SCRIPT_DIR="$(dirname "$0")"
nohup "$SCRIPT_DIR/stop_pulsemesh.sh" > /dev/null 2>&1 &
