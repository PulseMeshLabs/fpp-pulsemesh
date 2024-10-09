#!/bin/bash

echo "Running fpp-pulsemesh PostStart Script"

sudo systemctl restart pulsemesh-connector.service
