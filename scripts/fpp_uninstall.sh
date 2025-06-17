#!/bin/bash

SCRIPT_DIR="$(dirname "$0")"
$SCRIPT_DIR/stop_pulsemesh.sh

# Remote CSP for loading PulseMesh config page for FPP 9+
if [ -f "${FPPDIR}/scripts/ManageApacheContentPolicy.sh" ]; then
    ${FPPDIR}/scripts/ManageApacheContentPolicy.sh remove default-src "http://*:8089"
else
    echo "Skipping CSP removal: ManageApacheContentPolicy.sh not found"
fi
