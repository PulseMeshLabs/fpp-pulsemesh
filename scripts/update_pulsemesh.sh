#!/bin/bash

# PulseMesh Connector Update Script
# Checks for updates and downloads the latest binary if needed

set -euo pipefail  # Exit on error, undefined vars, pipe failures

# Configuration
VERSION_URL="https://pulsemesh.io/connectorapi/release/version"
DOWNLOAD_BASE_URL="https://pulsemesh.io/connectorapi/release/download/linux"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY_DIR="$(dirname "$SCRIPT_DIR")"
VERSION_FILE="$BINARY_DIR/pulsemesh_version.txt"
BINARY_NAME="pulsemesh-connector"
BINARY_PATH="$BINARY_DIR/$BINARY_NAME"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Function to get system architecture
get_architecture() {
    if command_exists dpkg; then
        dpkg --print-architecture
    else
        log_error "dpkg command not found. This script requires a Debian-based system."
        exit 1
    fi
}

# Function to make HTTP request with error handling
make_http_request() {
    local url="$1"
    local output_file="$2"
    local description="$3"
    
    if command_exists curl; then
        if ! curl -sSL --connect-timeout 10 --max-time 30 -o "$output_file" "$url"; then
            log_error "Failed to $description using curl"
            return 1
        fi
    elif command_exists wget; then
        if ! wget -q --timeout=30 --connect-timeout=10 -O "$output_file" "$url"; then
            log_error "Failed to $description using wget"
            return 1
        fi
    else
        log_error "Neither curl nor wget is available. Please install one of them."
        exit 1
    fi
    return 0
}

# Function to get remote version
get_remote_version() {
    local temp_file
    temp_file=$(mktemp)
    
    if ! make_http_request "$VERSION_URL" "$temp_file" "fetch version information"; then
        rm -f "$temp_file"
        return 1
    fi
    
    # Parse JSON response
    local version
    if command_exists jq; then
        version=$(jq -r '.version' "$temp_file" 2>/dev/null)
        if [[ "$version" == "null" || -z "$version" ]]; then
            log_error "Invalid JSON response or missing version field"
            rm -f "$temp_file"
            return 1
        fi
    else
        # Fallback JSON parsing without jq
        version=$(grep -o '"version":"[^"]*"' "$temp_file" 2>/dev/null | cut -d'"' -f4)
        if [[ -z "$version" ]]; then
            log_error "Failed to parse version from JSON response. Consider installing jq for better JSON parsing."
            rm -f "$temp_file"
            return 1
        fi
    fi
    
    rm -f "$temp_file"
    echo "$version"
    return 0
}

# Function to get local version
get_local_version() {
    if [[ -f "$VERSION_FILE" ]]; then
        cat "$VERSION_FILE" 2>/dev/null || echo ""
    else
        echo ""
    fi
}

# Function to download binary
download_binary() {
    local version="$1"
    local arch="$2"
    local download_url="$DOWNLOAD_BASE_URL/$arch"
    local temp_file
    temp_file=$(mktemp)
    
    log_info "Downloading PulseMesh Connector v$version for architecture: $arch"
    log_info "Download URL: $download_url"
    
    if ! make_http_request "$download_url" "$temp_file" "download binary"; then
        rm -f "$temp_file"
        return 1
    fi
    
    # Verify the downloaded file is not empty and appears to be a binary
    if [[ ! -s "$temp_file" ]]; then
        log_error "Downloaded file is empty"
        rm -f "$temp_file"
        return 1
    fi
    
    # Check if it's likely a binary file (not HTML error page)
    if file "$temp_file" | grep -q "HTML\|text"; then
        log_error "Downloaded file appears to be HTML/text, not a binary. Check if the download URL is correct."
        rm -f "$temp_file"
        return 1
    fi
    
    # Create binary directory if it doesn't exist
    mkdir -p "$BINARY_DIR"
    
    # Move the binary to final location
    if ! mv "$temp_file" "$BINARY_PATH"; then
        log_error "Failed to move binary to $BINARY_PATH"
        rm -f "$temp_file"
        return 1
    fi
    
    # Make binary executable
    chmod +x "$BINARY_PATH"
    
    log_info "Binary downloaded successfully to: $BINARY_PATH"
    return 0
}

# Function to update version file
update_version_file() {
    local version="$1"
    
    if ! echo "$version" > "$VERSION_FILE"; then
        log_error "Failed to update version file: $VERSION_FILE"
        return 1
    fi
    
    log_info "Version file updated: $version"
    return 0
}

# Main function
main() {
    log_info "Starting PulseMesh Connector update check..."
    
    # Get remote version
    log_info "Checking remote version..."
    local remote_version
    if ! remote_version=$(get_remote_version); then
        log_error "Failed to fetch remote version. Please check your internet connection and try again."
        exit 1
    fi
    log_info "Remote version: $remote_version"
    
    # Get local version
    local local_version
    local_version=$(get_local_version)
    
    if [[ -z "$local_version" ]]; then
        log_info "No local version file found. This appears to be the first run."
    else
        log_info "Local version: $local_version"
    fi
    
    # Compare versions
    if [[ "$local_version" == "$remote_version" ]]; then
        log_info "Local version matches remote version. No update needed."
        exit 0
    fi
    
    log_info "Version mismatch detected. Updating..."
    
    # Get system architecture only when we need to download
    local arch
    if ! arch=$(get_architecture); then
        exit 1
    fi
    log_info "System architecture: $arch"
    
    # Download new binary
    if ! download_binary "$remote_version" "$arch"; then
        log_error "Failed to download binary. Version file will not be updated."
        exit 1
    fi
    
    # Update version file only after successful download
    if ! update_version_file "$remote_version"; then
        log_warn "Binary was downloaded successfully, but failed to update version file."
        log_warn "The binary may be re-downloaded on the next run."
        exit 1
    fi
    
    log_info "Update completed successfully!"
    log_info "Binary location: $BINARY_PATH"
    log_info "Version: $remote_version"
}

# Trap to cleanup on script exit
cleanup() {
    # Remove any temporary files that might still exist
    find /tmp -name "tmp.*" -user "$(whoami)" -mmin +60 -delete 2>/dev/null || true
}

trap cleanup EXIT

# Run main function
main "$@"