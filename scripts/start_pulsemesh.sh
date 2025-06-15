#!/bin/bash

# PulseMesh Connector Start Script
# Starts the pulsemesh-connector service with logging and background execution

set -euo pipefail  # Exit on error, undefined vars, pipe failures

# Configuration
BINARY_NAME="pulsemesh-connector"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY_DIR="$(dirname "$SCRIPT_DIR")"
BINARY_PATH="$BINARY_DIR/$BINARY_NAME"
LOG_FILE="/home/fpp/media/logs/pulsemesh-connector.log"
PID_FILE="$BINARY_DIR/pulsemesh-connector.pid"

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

# Function to check if process is running
is_running() {
    if [[ -f "$PID_FILE" ]]; then
        local pid
        pid=$(cat "$PID_FILE" 2>/dev/null || echo "")
        if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
            return 0  # Process is running
        else
            # PID file exists but process is not running, clean up
            rm -f "$PID_FILE"
            return 1  # Process is not running
        fi
    fi
    return 1  # No PID file, process is not running
}

# Function to get running PID
get_running_pid() {
    if [[ -f "$PID_FILE" ]]; then
        cat "$PID_FILE" 2>/dev/null || echo ""
    else
        echo ""
    fi
}

# Function to create log directory
create_log_directory() {
    local log_dir
    log_dir=$(dirname "$LOG_FILE")
    
    if [[ ! -d "$log_dir" ]]; then
        log_info "Creating log directory: $log_dir"
        if ! mkdir -p "$log_dir"; then
            log_error "Failed to create log directory: $log_dir"
            return 1
        fi
    fi
    
    # Check if log directory is writable
    if [[ ! -w "$log_dir" ]]; then
        log_error "Log directory is not writable: $log_dir"
        return 1
    fi
    
    return 0
}

# Function to start the service
start_service() {
    log_info "Starting PulseMesh Connector..."
    
    # Check if binary exists
    if [[ ! -f "$BINARY_PATH" ]]; then
        log_error "Binary not found: $BINARY_PATH"
        log_error "Please run the update script first to download the binary."
        return 1
    fi
    
    # Check if binary is executable
    if [[ ! -x "$BINARY_PATH" ]]; then
        log_error "Binary is not executable: $BINARY_PATH"
        log_error "Attempting to make it executable..."
        if ! chmod +x "$BINARY_PATH"; then
            log_error "Failed to make binary executable"
            return 1
        fi
        log_info "Binary is now executable"
    fi
    
    # Check if already running
    if is_running; then
        local pid
        pid=$(get_running_pid)
        log_warn "PulseMesh Connector is already running (PID: $pid)"
        return 0
    fi
    
    # Create log directory
    if ! create_log_directory; then
        return 1
    fi
    
    # Add timestamp to log file
    echo "=== PulseMesh Connector started at $(date) ===" >> "$LOG_FILE"
    
    # Start the service with nohup and redirect output
    log_info "Starting service with logging to: $LOG_FILE"
    
    # Use nohup to start the process in background
    nohup "$BINARY_PATH" >> "$LOG_FILE" 2>&1 &
    local start_result=$?
    
    if [[ $start_result -ne 0 ]]; then
        log_error "Failed to start PulseMesh Connector"
        return 1
    fi
    
    # Get the PID of the started process
    local pid=$!
    
    # Save PID to file
    if ! echo "$pid" > "$PID_FILE"; then
        log_error "Failed to write PID file: $PID_FILE"
        log_warn "Service may have started but PID tracking will not work"
    fi
    
    # Give the process a moment to start
    sleep 2
    
    # Verify the process is still running
    if kill -0 "$pid" 2>/dev/null; then
        log_info "PulseMesh Connector started successfully (PID: $pid)"
        log_info "Logs: $LOG_FILE"
        return 0
    else
        log_error "PulseMesh Connector failed to start or crashed immediately"
        log_error "Check the log file for details: $LOG_FILE"
        rm -f "$PID_FILE"
        return 1
    fi
}

# Function to show status
show_status() {
    if is_running; then
        local pid
        pid=$(get_running_pid)
        log_info "PulseMesh Connector is running (PID: $pid)"
        
        # Show some process information if ps is available
        if command -v ps >/dev/null 2>&1; then
            echo "Process details:"
            ps -p "$pid" -o pid,ppid,cmd,etime,pcpu,pmem 2>/dev/null || true
        fi
    else
        log_info "PulseMesh Connector is not running"
    fi
}

# Main function
main() {
    case "${1:-start}" in
        start)
            start_service
            ;;
        status)
            show_status
            ;;
        *)
            echo "Usage: $0 [start|status]"
            echo "  start  - Start the PulseMesh Connector service (default)"
            echo "  status - Show service status"
            exit 1
            ;;
    esac
}

# Cleanup function
cleanup() {
    # Nothing specific to cleanup for start script
    true
}

trap cleanup EXIT

# Run main function
main "$@"