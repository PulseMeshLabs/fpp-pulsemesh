#!/bin/bash

echo "Running fpp-pulsemesh PreStop Script"

nohup ./stop_pulsemesh.sh >> /dev/null 2>&1 &
