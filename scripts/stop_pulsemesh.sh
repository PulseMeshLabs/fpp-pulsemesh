#!/bin/bash

# PulseMesh Connector Stop Script
# Stops the pulsemesh-connector service gracefully

set -euo pipefail  # Exit on error, undefined vars, pipe failures

# Configuration
BINARY_NAME="pulsemesh-connector"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PID_FILE="$SCRIPT_DIR/pulsemesh-connector.pid"
LOG_FILE="/home/fpp/media/logs/pulsemesh-connector.log"

# Timeouts (in seconds)
GRACEFUL_TIMEOUT=10
FORCE_TIMEOUT=5

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
    local pid="$1"
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
        return 0  # Process is running
    fi
    return 1  # Process is not running
}

# Function to get PID from file
get_pid_from_file() {
    if [[ -f "$PID_FILE" ]]; then
        cat "$PID_FILE" 2>/dev/null || echo ""
    else
        echo ""
    fi
}

# Function to find process by name (fallback)
find_process_by_name() {
    # Use pgrep if available, otherwise fallback to ps + grep
    if command -v pgrep >/dev/null 2>&1; then
        pgrep -f "$BINARY_NAME" 2>/dev/null | head -1 || echo ""
    else
        ps aux | grep -v grep | grep "$BINARY_NAME" | awk '{print $2}' | head -1 || echo ""
    fi
}

# Function to wait for process to stop
wait_for_stop() {
    local pid="$1"
    local timeout="$2"
    local count=0
    
    while [[ $count -lt $timeout ]]; do
        if ! is_running "$pid"; then
            return 0  # Process stopped
        fi
        sleep 1
        ((count++))
    done
    return 1  # Timeout reached
}

# Function to stop service gracefully
stop_service_graceful() {
    local pid="$1"
    
    log_info "Attempting graceful shutdown (SIGTERM)..."
    
    if ! kill -TERM "$pid" 2>/dev/null; then
        log_error "Failed to send SIGTERM to process $pid"
        return 1
    fi
    
    if wait_for_stop "$pid" $GRACEFUL_TIMEOUT; then
        log_info "Process stopped gracefully"
        return 0
    else
        log_warn "Process did not stop within $GRACEFUL_TIMEOUT seconds"
        return 1
    fi
}

# Function to stop service forcefully
stop_service_force() {
    local pid="$1"
    
    log_warn "Attempting forceful shutdown (SIGKILL)..."
    
    if ! kill -KILL "$pid" 2>/dev/null; then
        log_error "Failed to send SIGKILL to process $pid"
        return 1
    fi
    
    if wait_for_stop "$pid" $FORCE_TIMEOUT; then
        log_info "Process stopped forcefully"
        return 0
    else
        log_error "Process did not stop even after SIGKILL"
        return 1
    fi
}

# Function to cleanup PID file
cleanup_pid_file() {
    if [[ -f "$PID_FILE" ]]; then
        log_info "Removing PID file: $PID_FILE"
        rm -f "$PID_FILE"
    fi
}

# Function to add stop timestamp to log
add_stop_timestamp() {
    if [[ -f "$LOG_FILE" ]] && [[ -w "$LOG_FILE" ]]; then
        echo "=== PulseMesh Connector stopped at $(date) ===" >> "$LOG_FILE" 2>/dev/null || true
    fi
}

# Main stop function
stop_service() {
    log_info "Stopping PulseMesh Connector..."
    
    # Get PID from file
    local pid
    pid=$(get_pid_from_file)
    
    # If no PID file or empty, try to find process by name
    if [[ -z "$pid" ]]; then
        log_warn "No PID file found, searching for process by name..."
        pid=$(find_process_by_name)
        
        if [[ -z "$pid" ]]; then
            log_info "PulseMesh Connector is not running"
            cleanup_pid_file  # Clean up any stale PID file
            return 0
        else
            log_info "Found running process with PID: $pid"
        fi
    fi
    
    # Verify the process is actually running
    if ! is_running "$pid"; then
        log_info "Process $pid is not running"
        cleanup_pid_file
        return 0
    fi
    
    log_info "Stopping PulseMesh Connector (PID: $pid)"
    
    # Try graceful shutdown first
    if stop_service_graceful "$pid"; then
        cleanup_pid_file
        add_stop_timestamp
        log_info "PulseMesh Connector stopped successfully"
        return 0
    fi
    
    # If graceful shutdown failed, try force
    log_warn "Graceful shutdown failed, attempting force stop..."
    if stop_service_force "$pid"; then
        cleanup_pid_file
        add_stop_timestamp
        log_info "PulseMesh Connector stopped forcefully"
        return 0
    fi
    
    # If both failed
    log_error "Failed to stop PulseMesh Connector"
    log_error "You may need to manually kill the process (PID: $pid)"
    return 1
}

# Function to show status
show_status() {
    local pid
    pid=$(get_pid_from_file)
    
    if [[ -n "$pid" ]] && is_running "$pid"; then
        log_info "PulseMesh Connector is running (PID: $pid)"
        
        # Show some process information if ps is available
        if command -v ps >/dev/null 2>&1; then
            echo "Process details:"
            ps -p "$pid" -o pid,ppid,cmd,etime,pcpu,pmem 2>/dev/null || true
        fi
    else
        # Check if there's a process running by name but no PID file
        local running_pid
        running_pid=$(find_process_by_name)
        
        if [[ -n "$running_pid" ]]; then
            log_warn "PulseMesh Connector appears to be running (PID: $running_pid) but no PID file found"
            log_warn "This may indicate the process was started manually or the PID file was removed"
        else
            log_info "PulseMesh Connector is not running"
        fi
        
        # Clean up stale PID file if it exists
        if [[ -f "$PID_FILE" ]]; then
            log_info "Removing stale PID file"
            cleanup_pid_file
        fi
    fi
}

# Function to kill all processes by name (nuclear option)
kill_all() {
    log_warn "Attempting to kill all processes matching '$BINARY_NAME'..."
    
    if command -v pkill >/dev/null 2>&1; then
        if pkill -f "$BINARY_NAME"; then
            log_info "Killed processes using pkill"
        else
            log_info "No processes found to kill with pkill"
        fi
    else
        # Fallback method
        local pids
        pids=$(ps aux | grep -v grep | grep "$BINARY_NAME" | awk '{print $2}')
        
        if [[ -n "$pids" ]]; then
            echo "$pids" | while read -r pid; do
                if [[ -n "$pid" ]]; then
                    log_info "Killing process $pid"
                    kill -KILL "$pid" 2>/dev/null || true
                fi
            done
        else
            log_info "No processes found to kill"
        fi
    fi
    
    cleanup_pid_file
    add_stop_timestamp
}

# Main function
main() {
    case "${1:-stop}" in
        stop)
            stop_service
            ;;
        status)
            show_status
            ;;
        kill-all)
            kill_all
            ;;
        *)
            echo "Usage: $0 [stop|status|kill-all]"
            echo "  stop     - Stop the PulseMesh Connector service gracefully (default)"
            echo "  status   - Show service status"
            echo "  kill-all - Forcefully kill all PulseMesh Connector processes"
            exit 1
            ;;
    esac
}

# Cleanup function
cleanup() {
    # Nothing specific to cleanup for stop script
    true
}

trap cleanup EXIT

# Run main function
main "$@"