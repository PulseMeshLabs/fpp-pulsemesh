#!/bin/bash

# fpp-pulsemesh install script

# Exit immediately if a command exits with a non-zero status
set -e

BASEDIR=$(dirname "$0")
cd "$BASEDIR" || exit
cd ..

make "SRCDIR=${SRCDIR}"

# Run cleanup commands - continue even if they fail
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

# Run pulsemesh scripts
echo "Running pulsemesh scripts..."

if [ -x "${BASEDIR}/stop_pulsemesh.sh" ]; then
    echo "Stopping pulsemesh..."
    "${BASEDIR}/stop_pulsemesh.sh kill-all" || {
        echo "Warning: stop_pulsemesh.sh kill-all failed with exit code $?"
    }
else
    echo "Warning: stop_pulsemesh.sh not found or not executable"
fi

if [ -x "${BASEDIR}/update_pulsemesh.sh" ]; then
    echo "Updating pulsemesh..."
    "${BASEDIR}/update_pulsemesh.sh" || {
        echo "Warning: update_pulsemesh.sh failed with exit code $?"
    }
else
    echo "Warning: update_pulsemesh.sh not found or not executable"
fi

if [ -x "${BASEDIR}/start_pulsemesh.sh" ]; then
    echo "Starting pulsemesh..."
    "${BASEDIR}/start_pulsemesh.sh" || {
        echo "Warning: start_pulsemesh.sh failed with exit code $?"
    }
else
    echo "Warning: start_pulsemesh.sh not found or not executable"
fi

# Source common scripts and set restart flag
if [ -f "${FPPDIR}/scripts/common" ]; then
    . "${FPPDIR}/scripts/common"
    setSetting restartFlag 1
fi

echo "fpp-pulsemesh installation script completed."




#!/bin/bash

# fpp-pulsemesh install script

# Exit immediately if a command exits with a non-zero status
set -e

BASEDIR=$(dirname "$0")
cd "$BASEDIR" || exit
cd ..

make "SRCDIR=${SRCDIR}"

# Function to install in non-Docker environments
install_non_docker() {
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
}

# Function to install in Docker environments
install_docker() {
    echo "Detected Docker environment. Proceeding with Docker installation..."

    # Determine architecture
    ARCH=$(dpkg --print-architecture)
    echo "Detected architecture: $ARCH"

    # Define download URL
    DOWNLOAD_URL="https://files.pulsemesh.io/pulsemesh-connector-$ARCH"

    # Define installation directory
    INSTALL_DIR="/home/fpp/media/plugins/fpp-PulseMesh"
    EXECUTABLE_NAME="pulsemesh-connector"

    # Download the executable
    echo "Downloading PulseMesh Connector from $DOWNLOAD_URL..."
    wget -q "$DOWNLOAD_URL" -O "$INSTALL_DIR/$EXECUTABLE_NAME"

    # Make the executable runnable
    echo "Making the PulseMesh Connector executable..."
    chmod +x "$INSTALL_DIR/$EXECUTABLE_NAME"

    # Start the executable in the background
    echo "Starting PulseMesh Connector..."
    nohup "$INSTALL_DIR/$EXECUTABLE_NAME" > "/home/fpp/media/logs/pulsemesh-connector.log" 2>&1 &
    echo $! > "$INSTALL_DIR/pulsemesh.pid"

    # Source common scripts and set restart flag
    if [ -f "${FPPDIR}/scripts/common" ]; then
        . "${FPPDIR}/scripts/common"
        setSetting restartFlag 1
    fi

    echo "Docker installation completed successfully."
}

# Check if running inside Docker
if [ -f "/.dockerenv" ]; then
    install_docker
else
    install_non_docker
fi
