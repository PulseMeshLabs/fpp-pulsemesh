#!/bin/bash

# PulseMesh Connector Update Script
# Checks for updates and downloads the latest binary if needed

set -euo pipefail  # Exit on error, undefined vars, pipe failures

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY_DIR="$(dirname "$SCRIPT_DIR")"
BINARY_NAME="pulsemesh-connector"
BINARY_PATH="$BINARY_DIR/$BINARY_NAME"

# Shared config file (read by the plugin and both connector binaries). A
# top-level `V2 = 1` key selects the v2 (Rust) artifact instead of the
# legacy Go one. See pulsemesh-connector-rs/docs/fpp-install.md.
CONFIG_FILE="/home/fpp/media/config/plugin.fpp-pulsemesh"

# Legacy (Go) artifact endpoints and version file (defaults).
VERSION_URL="https://pulsemesh.io/connectorapi/release/version"
DOWNLOAD_BASE_URL="https://pulsemesh.io/connectorapi/release/download/linux"
VERSION_FILE="$BINARY_DIR/pulsemesh_version.txt"

# Function to read the top-level V2 flag from the shared config file.
# Tolerant parse: accepts `V2 = 1`, `V2=1`, leading spaces, and either
# case of the key. Only the top-level key is read; the `[v2]` section that
# the v2 binary consumes is left untouched. Echoes "1" when enabled, "" otherwise.
get_v2_flag() {
    if [[ -f "$CONFIG_FILE" ]]; then
        sed -n 's/^[[:space:]]*[Vv]2[[:space:]]*=[[:space:]]*//p' "$CONFIG_FILE" 2>/dev/null \
            | head -1 | tr -d '[:space:]'
    fi
}

# Select the v2 artifact endpoints and a SEPARATE version file when the flag
# is set. Using a distinct version file (pulsemesh_version_v2.txt vs
# pulsemesh_version.txt) guarantees that flipping the flag in EITHER direction
# always mismatches the recorded version and forces a fresh download of the
# correct binary, rather than trusting a stale version match from the other track.
V2_FLAG="$(get_v2_flag)"
if [[ "$V2_FLAG" == "1" ]]; then
    VERSION_URL="https://pulsemesh.io/connectorapi/release-v2/version"
    DOWNLOAD_BASE_URL="https://pulsemesh.io/connectorapi/release-v2/download/linux"
    VERSION_FILE="$BINARY_DIR/pulsemesh_version_v2.txt"
fi

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Disable colors for output
RED=''
GREEN=''
YELLOW=''
NC='' 

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
#
# The fourth argument selects the timeout policy, which must differ by payload
# size. A wall-clock cap that is generous for the few-byte version JSON is a
# hard install failure for the ~7.5MB connector on any link slower than about
# 250KB/s, so the "large" policy aborts only on a stalled transfer (no bytes
# for STALL_SECONDS) rather than on elapsed time, and resumes across retries.
STALL_BYTES_PER_SEC=1024
STALL_SECONDS=15
SMALL_MAX_TIME=30
RETRY_COUNT=3
RETRY_DELAY=3

# Caps total time spent retrying, so an unreachable host fails in well under a
# minute instead of serially burning every retry's full timeout. curl checks
# this before starting each retry, so the true worst case is this limit plus one
# final attempt's timeout.
RETRY_MAX_TIME=30

make_http_request() {
    local url="$1"
    local output_file="$2"
    local description="$3"
    local size_policy="${4:-small}"

    if command_exists curl; then
        # --fail turns an error page into a non-zero exit instead of a body that
        # would later be misdiagnosed by the binary sniff in download_binary.
        local curl_args=(--fail -sSL --connect-timeout 10
                         --retry "$RETRY_COUNT" --retry-delay "$RETRY_DELAY" --retry-connrefused
                         --retry-max-time "$RETRY_MAX_TIME")
        if [[ "$size_policy" == "large" ]]; then
            # -C - makes curl's own retries resume the partial file rather than
            # restart it; on a 0-byte temp file it simply starts from the top.
            curl_args+=(--speed-limit "$STALL_BYTES_PER_SEC" --speed-time "$STALL_SECONDS" -C -)
        else
            curl_args+=(--max-time "$SMALL_MAX_TIME")
        fi

        if ! curl "${curl_args[@]}" -o "$output_file" "$url"; then
            log_error "Failed to $description using curl"
            return 1
        fi
    elif command_exists wget; then
        # wget's --read-timeout is already per-read, so it matches the "large"
        # stall semantics above; -c is deliberately omitted because wget refuses
        # to combine continuation with -O.
        local wget_args=(-q --dns-timeout=10 --connect-timeout=10
                         --tries="$RETRY_COUNT" --waitretry="$RETRY_DELAY")
        if [[ "$size_policy" == "large" ]]; then
            wget_args+=(--read-timeout="$STALL_SECONDS")
        else
            wget_args+=(--read-timeout="$SMALL_MAX_TIME")
        fi

        if ! wget "${wget_args[@]}" -O "$output_file" "$url"; then
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

# Verify a downloaded file against a SHA-256 checksum published beside the
# binary (<download-url>.sha256, containing the hex digest as its first word).
# A fetched checksum that mismatches is fatal; a missing checksum (endpoint
# not yet published for this release track) only warns, so verification
# hardens automatically once the server publishes digests without bricking
# installs until then.
verify_checksum() {
    local file="$1"
    local checksum_url="$2"
    local checksum_file expected actual
    checksum_file=$(mktemp)

    if ! make_http_request "$checksum_url" "$checksum_file" "fetch checksum (optional)"; then
        log_warn "No checksum published at $checksum_url; skipping verification."
        rm -f "$checksum_file"
        return 0
    fi

    expected=$(awk '{print tolower($1); exit}' "$checksum_file")
    rm -f "$checksum_file"
    if [[ ! "$expected" =~ ^[0-9a-f]{64}$ ]]; then
        log_error "Checksum file at $checksum_url is malformed"
        return 1
    fi

    actual=$(sha256sum "$file" | awk '{print $1}')
    if [[ "$actual" != "$expected" ]]; then
        log_error "SHA-256 mismatch for downloaded binary (expected $expected, got $actual)"
        return 1
    fi

    log_info "SHA-256 checksum verified."
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
    
    if ! make_http_request "$download_url" "$temp_file" "download binary" "large"; then
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

    # Verify against the published SHA-256 digest before trusting the file
    if ! verify_checksum "$temp_file" "$download_url.sha256"; then
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

    if [[ "$V2_FLAG" == "1" ]]; then
        log_info "V2 flag is set — selecting v2 (Rust) connector artifact."
    else
        log_info "V2 flag is not set — selecting legacy (Go) connector artifact."
    fi
    log_info "Version file: $VERSION_FILE"

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