#!/bin/bash

SCRIPT_DIR="$(dirname "$0")"
$SCRIPT_DIR/stop_pulsemesh.sh

# Remove CSP for PulseMesh config page for FPP 9+ (skip if running in Docker)
if [ ! -f "/.dockerenv" ]; then
    if [ -f "${FPPDIR}/scripts/ManageApacheContentPolicy.sh" ]; then
        ${FPPDIR}/scripts/ManageApacheContentPolicy.sh remove default-src "http://*:8089"
        ${FPPDIR}/scripts/ManageApacheContentPolicy.sh remove default-src "https://*:8089"
    else
        echo "Skipping CSP removal: ManageApacheContentPolicy.sh not found (FPP version < 9)"
    fi
else
    echo "Skipping CSP removal: Running in Docker environment and CSP is not currently supported"
fi