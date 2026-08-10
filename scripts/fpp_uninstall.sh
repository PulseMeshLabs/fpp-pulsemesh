#!/bin/bash

SCRIPT_DIR="$(dirname "$0")"
$SCRIPT_DIR/stop_pulsemesh.sh

# Remove CSP for PulseMesh config page for FPP 9+
if [ -f "${FPPDIR}/scripts/ManageApacheContentPolicy.sh" ]; then
    if [ -f "/.dockerenv" ]; then
        # In Docker, the script may return non-zero but we continue anyway
        ${FPPDIR}/scripts/ManageApacheContentPolicy.sh remove default-src "http://*:8089" || true
        ${FPPDIR}/scripts/ManageApacheContentPolicy.sh remove default-src "https://*:8089" || true
        echo "CSP removal attempted in Docker environment (errors expected and ignored)"
    else
        ${FPPDIR}/scripts/ManageApacheContentPolicy.sh remove default-src "http://*:8089"
        ${FPPDIR}/scripts/ManageApacheContentPolicy.sh remove default-src "https://*:8089"
    fi
else
    echo "Skipping CSP removal: ManageApacheContentPolicy.sh not found (FPP version < 9)"
fi

# FPP majors before 10 have no runtime plugin unload, and this script is the
# only code that runs before the plugin directory is deleted — without the
# restart flag fppd keeps the stale plugin loaded until an unrelated restart.
if [ -f "${FPPDIR}/scripts/common" ]; then
    . "${FPPDIR}/scripts/common"
    setSetting restartFlag 1
fi