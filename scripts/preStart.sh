#!/bin/bash

echo "Running fpp-pulsemesh PreStart Script"

nohup ./stop_pulsemesh.sh > /home/fpp/media/logs/pulsemesh-connector-setup.log 2>&1 &