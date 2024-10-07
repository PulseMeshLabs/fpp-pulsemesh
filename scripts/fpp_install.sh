#!/bin/bash

# fpp-pulsemesh install script

# Exit immediately if a command exits with a non-zero status
set -e

BASEDIR=$(dirname "$0")
cd "$BASEDIR" || exit
cd ..

make "SRCDIR=${SRCDIR}"

# Define variables for key and repository
KEY_URL="https://repo.pulsemesh.io/pulsemsh-repo-key.gpg"
KEYRING="/usr/share/keyrings/pulsemsh-repo-keyring.gpg"
REPO_LIST="/etc/apt/sources.list.d/pulsemsh.list"

# Download the GPG key and convert it to binary format
echo "Downloading and adding the PulseMesh GPG key..."
wget -qO- "$KEY_URL" | gpg --dearmor | sudo tee "$KEYRING" > /dev/null

# Verify that the key contains the required key ID
echo "Verifying the GPG key..."
if ! gpg --no-default-keyring --keyring "$KEYRING" --list-keys | grep -q "F0889C1D4BDAC295"; then
    echo "Error: The GPG key does not contain the required key ID F0889C1D4BDAC295."
    exit 1
fi

# Add the repository with the signed-by option
echo "Adding the PulseMesh repository..."
echo "deb [arch=amd64,arm64,armhf signed-by=$KEYRING] https://repo.pulsemesh.io stable main" | sudo tee "$REPO_LIST" > /dev/null

# Update the package list
echo "Updating package lists..."
sudo apt update

# Install the PulseMesh Connector
echo "Installing PulseMesh Connector..."
sudo apt install -y pulsemesh-connector

# Enable and start the PulseMesh Connector service
echo "Enabling and starting PulseMesh Connector service..."
sudo systemctl enable pulsemesh-connector.service
sudo systemctl start pulsemesh-connector.service

# Source common scripts and set restart flag
. "${FPPDIR}/scripts/common"
setSetting restartFlag 1

echo "Installation completed successfully."
