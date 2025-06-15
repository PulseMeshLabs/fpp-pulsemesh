#!/bin/bash

# PulseMesh Connector Restart Script
# Stops the service, updates the binary, and starts the service

set -euo pipefail  # Exit on error, undefined vars, pipe failures

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STOP_SCRIPT="$SCRIPT_DIR/stop_pulsemesh.sh"
UPDATE_SCRIPT="$SCRIPT_DIR/update_pulsemesh.sh"
START_SCRIPT="$SCRIPT_DIR/start_pulsemesh.sh"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
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

log_step() {
    echo -e "${BLUE}[STEP]${NC} $1"
}

# Function to check if script exists and is executable
check_script() {
    local script_path="$1"
    local script_name="$2"
    
    if [[ ! -f "$script_path" ]]; then
        log_error "$script_name not found: $script_path"
        return 1
    fi
    
    if [[ ! -x "$script_path" ]]; then
        log_warn "$script_name is not executable: $script_path"
        log_info "Attempting to make it executable..."
        if ! chmod +x "$script_path"; then
            log_error "Failed to make $script_name executable"
            return 1
        fi
        log_info "$script_name is now executable"
    fi
    
    return 0
}

# Function to run a script with error handling
run_script() {
    local script_path="$1"
    local script_name="$2"
    local step_number="$3"
    
    log_step "Step $step_number: Running $script_name..."
    
    if ! "$script_path"; then
        log_error "$script_name failed (exit code: $?)"
        return 1
    fi
    
    log_info "$script_name completed successfully"
    return 0
}

# Function to show usage
show_usage() {
    echo "Usage: $0 [--force]"
    echo ""
    echo "This script performs a complete restart of PulseMesh Connector:"
    echo "  1. Stops the running service"
    echo "  2. Updates the binary to the latest version"
    echo "  3. Starts the service with the new binary"
    echo ""
    echo "Options:"
    echo "  --force    Skip confirmation prompt"
    echo "  --help     Show this help message"
}

# Function to confirm action
confirm_restart() {
    if [[ "${1:-}" == "--force" ]]; then
        return 0
    fi
    
    echo ""
    log_warn "This will restart PulseMesh Connector with the following steps:"
    echo "  1. Stop the current service"
    echo "  2. Check for and download any updates"
    echo "  3. Start the service with the (potentially updated) binary"
    echo ""
    echo -n "Do you want to continue? (y/N): "
    read -r response
    
    case "$response" in
        [yY]|[yY][eE][sS])
            return 0
            ;;
        *)
            log_info "Operation cancelled by user"
            exit 0
            ;;
    esac
}

# Main restart function
restart_service() {
    local force_flag="${1:-}"
    
    log_info "=== PulseMesh Connector Restart Script ==="
    log_info "Starting restart process at $(date)"
    
    # Confirm action unless --force is used
    confirm_restart "$force_flag"
    
    echo ""
    log_info "Checking required scripts..."
    
    # Check if all required scripts exist and are executable
    if ! check_script "$STOP_SCRIPT" "Stop script"; then
        exit 1
    fi
    
    if ! check_script "$UPDATE_SCRIPT" "Update script"; then
        exit 1
    fi
    
    if ! check_script "$START_SCRIPT" "Start script"; then
        exit 1
    fi
    
    log_info "All required scripts found and executable"
    echo ""
    
    # Step 1: Stop the service
    if ! run_script "$STOP_SCRIPT" "stop_pulsemesh.sh" "1"; then
        log_error "Failed to stop PulseMesh Connector"
        log_error "Cannot proceed with restart. Please check the stop script output above."
        exit 1
    fi
    
    echo ""
    
    # Step 2: Update the binary
    if ! run_script "$UPDATE_SCRIPT" "update_pulsemesh.sh" "2"; then
        log_error "Failed to update PulseMesh Connector"
        log_error "Attempting to start with existing binary..."
        
        # Try to start with existing binary
        echo ""
        if run_script "$START_SCRIPT" "start_pulsemesh.sh" "3 (recovery)"; then
            log_warn "Service started with existing binary (update failed)"
            exit 1
        else
            log_error "Failed to start service even with existing binary"
            log_error "Manual intervention required"
            exit 1
        fi
    fi
    
    echo ""
    
    # Step 3: Start the service
    if ! run_script "$START_SCRIPT" "start_pulsemesh.sh" "3"; then
        log_error "Failed to start PulseMesh Connector"
        log_error "The binary was updated but failed to start"
        log_error "Check the logs and try starting manually"
        exit 1
    fi
    
    echo ""
    log_info "=== Restart Complete ==="
    log_info "PulseMesh Connector has been successfully restarted"
    log_info "Process completed at $(date)"
    
    # Show final status
    echo ""
    log_info "Final service status:"
    if command -v "$START_SCRIPT" >/dev/null 2>&1; then
        "$START_SCRIPT" status || true
    fi
}

# Function to show status of all components
show_status() {
    log_info "=== PulseMesh Connector Status ==="
    
    # Check script availability
    echo "Script availability:"
    for script in "$STOP_SCRIPT" "$UPDATE_SCRIPT" "$START_SCRIPT"; do
        local name
        name=$(basename "$script")
        if [[ -f "$script" ]]; then
            if [[ -x "$script" ]]; then
                echo "  ✓ $name (executable)"
            else
                echo "  ! $name (not executable)"
            fi
        else
            echo "  ✗ $name (missing)"
        fi
    done
    
    echo ""
    
    # Show service status
    if [[ -f "$START_SCRIPT" ]] && [[ -x "$START_SCRIPT" ]]; then
        "$START_SCRIPT" status || true
    else
        log_warn "Cannot check service status (start script not available)"
    fi
}

# Main function
main() {
    case "${1:-restart}" in
        restart|--force)
            restart_service "$1"
            ;;
        status)
            show_status
            ;;
        --help|-h|help)
            show_usage
            ;;
        *)
            echo "Unknown option: $1"
            echo ""
            show_usage
            exit 1
            ;;
    esac
}

# Cleanup function
cleanup() {
    # Nothing specific to cleanup for restart script
    true
}

trap cleanup EXIT

# Run main function
main "$@"