#!/bin/bash
# Proton Drive Linux - Sync Service Setup Script
# Creates and enables the systemd user service for mounting Proton Drive

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_status() { echo -e "${BLUE}[*]${NC} $1"; }
print_success() { echo -e "${GREEN}[✓]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[!]${NC} $1"; }
print_error() { echo -e "${RED}[✗]${NC} $1"; }

# Configuration
REMOTE_NAME="proton"
MOUNT_POINT="$HOME/ProtonDrive"
SERVICE_NAME="proton-drive-sync"
SYSTEMD_DIR="$HOME/.config/systemd/user"
SYSTEMD_UNIT="$SYSTEMD_DIR/$SERVICE_NAME.service"
RCLONE_BIN=$(command -v rclone || echo "/usr/local/bin/rclone")
LOG_DIR="$HOME/.cache/proton-drive"
LOG_FILE="$LOG_DIR/rclone.log"

# Check if running as root
if [ "$EUID" -eq 0 ]; then
    print_error "Please do not run this script as root"
    exit 1
fi

# Check prerequisites
check_prerequisites() {
    local missing=0
    
    if ! command -v rclone &> /dev/null; then
        print_error "rclone is not installed. Run ./scripts/setup-rclone.sh first"
        missing=1
    fi
    
    if ! rclone listremotes | grep -q "^proton:"; then
        print_error "Proton Drive remote not configured. Run ./scripts/setup-rclone.sh first"
        missing=1
    fi
    
    if ! command -v fusermount3 &> /dev/null; then
        print_error "fuse3 is not installed. Run ./scripts/setup-rclone.sh first"
        missing=1
    fi
    
    return $missing
}

# Create mount point
create_mount_point() {
    print_status "Creating mount point at $MOUNT_POINT..."
    mkdir -p "$MOUNT_POINT"
    mkdir -p "$LOG_DIR"
    mkdir -p "$SYSTEMD_DIR"
    print_success "Mount point created"
}

# Write systemd service file
create_service() {
    print_status "Creating systemd service..."
    
    cat > "$SYSTEMD_UNIT" <<EOF
[Unit]
Description=Mount Proton Drive via rclone
After=network-online.target
Wants=network-online.target

[Service]
Type=notify
ExecStart=$RCLONE_BIN mount $REMOTE_NAME: $MOUNT_POINT \\
    --vfs-cache-mode writes \\
    --vfs-cache-max-size 500M \\
    --vfs-cache-max-age 1h \\
    --dir-cache-time 5m \\
    --poll-interval 1m \\
    --rc \\
    --rc-addr=localhost:5572 \\
    --rc-no-auth \\
    --log-level INFO \\
    --log-file $LOG_FILE \\
    --umask 002 \\
    --allow-other
ExecStop=/bin/fusermount3 -u $MOUNT_POINT
Restart=on-failure
RestartSec=10

[Install]
WantedBy=default.target
EOF

    print_success "Systemd service created at $SYSTEMD_UNIT"
}

# Enable and start service
start_service() {
    print_status "Enabling and starting sync service..."
    
    # Reload systemd
    systemctl --user daemon-reload
    
    # Enable the service to start at login
    systemctl --user enable "$SERVICE_NAME.service"
    
    # Start the service now
    systemctl --user start "$SERVICE_NAME.service"
    
    # Wait a moment for mount
    sleep 2
    
    # Check if mounted
    if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
        print_success "Proton Drive mounted successfully!"
    else
        print_warning "Mount may still be initializing. Check status with: systemctl --user status $SERVICE_NAME"
    fi
}

# Show status
show_status() {
    echo ""
    print_status "Service status:"
    systemctl --user status "$SERVICE_NAME.service" --no-pager || true
    
    echo ""
    if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
        print_success "Proton Drive is mounted at: $MOUNT_POINT"
        echo ""
        echo "Contents:"
        ls -la "$MOUNT_POINT" 2>/dev/null | head -10
    else
        print_warning "Proton Drive is not currently mounted"
    fi
}

# Stop and disable service
stop_service() {
    print_status "Stopping sync service..."
    systemctl --user stop "$SERVICE_NAME.service" || true
    systemctl --user disable "$SERVICE_NAME.service" || true
    print_success "Sync service stopped and disabled"
}

# Uninstall
uninstall() {
    print_status "Uninstalling sync service..."
    
    stop_service
    
    if [ -f "$SYSTEMD_UNIT" ]; then
        rm "$SYSTEMD_UNIT"
        systemctl --user daemon-reload
        print_success "Service file removed"
    fi
    
    print_warning "Mount point at $MOUNT_POINT was not removed (in case you have local changes)"
    print_success "Uninstall complete"
}

# Main
main() {
    echo ""
    echo "╔═══════════════════════════════════════════════════════════╗"
    echo "║       Proton Drive Linux - Sync Service Setup             ║"
    echo "╚═══════════════════════════════════════════════════════════╝"
    echo ""
    
    case "${1:-}" in
        --status)
            show_status
            exit 0
            ;;
        --stop)
            stop_service
            exit 0
            ;;
        --start)
            print_status "Starting sync service..."
            systemctl --user start "$SERVICE_NAME.service"
            show_status
            exit 0
            ;;
        --uninstall)
            uninstall
            exit 0
            ;;
        --help)
            echo "Usage: $0 [OPTION]"
            echo ""
            echo "Options:"
            echo "  (no option)   Setup and enable sync service"
            echo "  --status      Show mount status"
            echo "  --start       Start the sync service"
            echo "  --stop        Stop the sync service"
            echo "  --uninstall   Remove the sync service"
            echo "  --help        Show this help"
            exit 0
            ;;
    esac
    
    # Check prerequisites
    if ! check_prerequisites; then
        exit 1
    fi
    
    # Setup
    create_mount_point
    create_service
    start_service
    show_status
    
    echo ""
    print_success "Sync setup complete!"
    echo ""
    echo "Your Proton Drive is now mounted at: $MOUNT_POINT"
    echo ""
    echo "Useful commands:"
    echo "  Check status:  ./scripts/setup-sync-service.sh --status"
    echo "  View logs:     tail -f $LOG_FILE"
    echo "  Stop sync:     ./scripts/setup-sync-service.sh --stop"
    echo "  Start sync:    ./scripts/setup-sync-service.sh --start"
    echo ""
}

main "$@"
