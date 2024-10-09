#!/bin/bash

echo "Running fpp-pulsemesh PreStop Script"

sudo systemctl stop pulsemesh-connector.service
