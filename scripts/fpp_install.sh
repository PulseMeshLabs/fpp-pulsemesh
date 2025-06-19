#!/bin/bash

# fpp-pulsemesh install script

# Exit immediately if a command exits with a non-zero status
set -e

BASEDIR=$(dirname "$0")
cd "$BASEDIR" || exit
cd ..

make "SRCDIR=${SRCDIR}"

# Run cleanup commands if legacy install is present
if [ -f "/usr/local/bin/pulsemesh-connector" ]; then
    echo "Cleaning up possible existing pulsemesh-connector installation..."

    sudo systemctl --now disable pulsemesh-connector.service 2>/dev/null || {
        echo "Didn't disable pulsemesh-connector.service (may not exist, this is OK)"
    }

    sudo apt-get purge -y pulsemesh-connector 2>/dev/null || {
        echo "Didn't purge pulsemesh-connector package (may not be installed, this is OK)"
    }

    sudo rm -f /etc/apt/sources.list.d/pulsemsh.list 2>/dev/null || {
        echo "Didn't remove /etc/apt/sources.list.d/pulsemsh.list (may not exist, this is OK)"
    }

    echo "Cleanup completed."
fi

# Run pulsemesh scripts
echo "Running pulsemesh install scripts..."

if [ -x "${BASEDIR}/stop_pulsemesh.sh" ]; then
    echo "Stopping pulsemesh..."
    "${BASEDIR}/stop_pulsemesh.sh" kill-all || {
        echo "Warning: stop_pulsemesh.sh kill-all failed with exit code $?"
    }
else
    echo "Warning: stop_pulsemesh.sh not found or not executable"
fi

if [ -x "${BASEDIR}/update_pulsemesh.sh" ]; then
    echo "Updating pulsemesh..."
    "${BASEDIR}/update_pulsemesh.sh" || {
        echo "Error: update_pulsemesh.sh failed with exit code $?" 
        exit 1
    }
else
    echo "Error: update_pulsemesh.sh not found or not executable"
    exit 1
fi

if [ -x "${BASEDIR}/start_pulsemesh.sh" ]; then
    echo "Starting pulsemesh..."
    "${BASEDIR}/start_pulsemesh.sh" || {
        echo "Error: start_pulsemesh.sh failed with exit code $?"
        exit 1
    }
else
    echo "Error: start_pulsemesh.sh not found or not executable"
    exit 1
fi

# Add CSP for loading PulseMesh config page for FPP 9+ (skip if running in Docker)
if [ ! -f "/.dockerenv" ]; then
    if [ -f "${FPPDIR}/scripts/ManageApacheContentPolicy.sh" ]; then
        ${FPPDIR}/scripts/ManageApacheContentPolicy.sh add default-src "http://*:8089"
        ${FPPDIR}/scripts/ManageApacheContentPolicy.sh add default-src "https://*:8089"
    else
        echo "Skipping CSP addition: ManageApacheContentPolicy.sh not found (FPP version < 9)"
    fi
else
    echo "Skipping CSP addition: Running in Docker environment and CSP is not currently supported"
fi

# Source common scripts and set restart flag
if [ -f "${FPPDIR}/scripts/common" ]; then
    . "${FPPDIR}/scripts/common"
    setSetting restartFlag 1
fi

echo "fpp-pulsemesh installation script completed."