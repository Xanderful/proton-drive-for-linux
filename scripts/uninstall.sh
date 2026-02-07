#!/bin/bash
# Proton Drive Linux - Uninstaller Script
# Removes installed components based on user selection

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${BLUE}╔═══════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║     Proton Drive Linux - Uninstaller                       ║${NC}"
echo -e "${BLUE}╚═══════════════════════════════════════════════════════════╝${NC}"
echo ""

# Detect installation type
DESKTOP_ENTRY_USER="$HOME/.local/share/applications/proton-drive.desktop"
DESKTOP_ENTRY_SYSTEM="/usr/share/applications/proton-drive.desktop"
BINARY_LOCAL="$(pwd)/src-native/build/proton-drive"
BINARY_SYSTEM="/usr/local/bin/proton-drive"
ICON_USER="$HOME/.local/share/icons/hicolor/scalable/apps/proton-drive.svg"
ICON_SYSTEM="/usr/share/icons/hicolor/scalable/apps/proton-drive.svg"

INSTALL_TYPE="none"
if [ -f "$DESKTOP_ENTRY_SYSTEM" ] || [ -f "$BINARY_SYSTEM" ]; then
    INSTALL_TYPE="system"
elif [ -f "$DESKTOP_ENTRY_USER" ] || [ -f "$BINARY_LOCAL" ]; then
    INSTALL_TYPE="user"
fi

if [ "$INSTALL_TYPE" = "none" ]; then
    echo -e "${YELLOW}No Proton Drive installation found.${NC}"
    echo -e "${BLUE}Would you like to clean up any leftover files? (y/n)${NC}"
    read -r response
    if [[ ! "$response" =~ ^[Yy]$ ]]; then
        echo "Exiting."
        exit 0
    fi
fi

echo -e "${BLUE}What would you like to uninstall?${NC}"
echo "  1) Remove application only (keep user data and sync setup)"
echo "  2) Remove application + user data"
echo "  3) Remove application + sync setup (rclone config and systemd service)"
echo "  4) Complete uninstall (application + user data + sync setup)"
echo "  5) Exit"
echo ""
echo -n "Choose an option [1-5]: "
read -r choice

case $choice in
    5)
        echo "Exiting."
        exit 0
        ;;
    1|2|3|4)
        # Continue with uninstall
        ;;
    *)
        echo -e "${RED}Invalid option. Exiting.${NC}"
        exit 1
        ;;
esac

# Function to remove application files
remove_application() {
    echo -e "${BLUE}[*] Removing Proton Drive application...${NC}"
    
    # Stop any running instances
    if pgrep -f "proton-drive" > /dev/null; then
        echo -e "${YELLOW}  Stopping running Proton Drive instances...${NC}"
        pkill -SIGTERM -f "proton-drive" 2>/dev/null || true
        sleep 2
        pkill -9 -f "proton-drive" 2>/dev/null || true
    fi
    
    # Remove desktop entry
    if [ -f "$DESKTOP_ENTRY_USER" ]; then
        echo -e "${BLUE}  Removing user desktop entry...${NC}"
        rm -f "$DESKTOP_ENTRY_USER"
    fi
    
    if [ -f "$DESKTOP_ENTRY_SYSTEM" ]; then
        echo -e "${BLUE}  Removing system desktop entry...${NC}"
        sudo rm -f "$DESKTOP_ENTRY_SYSTEM"
    fi
    
    # Remove icons
    if [ -f "$ICON_USER" ]; then
        echo -e "${BLUE}  Removing user icon...${NC}"
        rm -f "$ICON_USER"
        gtk-update-icon-cache "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
    fi
    
    if [ -f "$ICON_SYSTEM" ]; then
        echo -e "${BLUE}  Removing system icon...${NC}"
        sudo rm -f "$ICON_SYSTEM"
        sudo gtk-update-icon-cache /usr/share/icons/hicolor 2>/dev/null || true
    fi
    
    # Remove binary
    if [ -f "$BINARY_SYSTEM" ]; then
        echo -e "${BLUE}  Removing system binary...${NC}"
        sudo rm -f "$BINARY_SYSTEM"
    fi
    
    # Note: Don't remove local build directory as it's part of source tree
    if [ -f "$BINARY_LOCAL" ]; then
        echo -e "${YELLOW}  Local build remains at: $BINARY_LOCAL${NC}"
        echo -e "${YELLOW}  (Part of source tree - run 'make clean' to remove)${NC}"
    fi
    
    echo -e "${GREEN}[✓] Application removed${NC}"
}

# Function to remove user data
remove_user_data() {
    echo -e "${BLUE}[*] Removing user data...${NC}"
    
    # Application data directory
    DATA_DIR="$HOME/.local/share/proton-drive"
    if [ -d "$DATA_DIR" ]; then
        echo -e "${BLUE}  Removing $DATA_DIR${NC}"
        rm -rf "$DATA_DIR"
    fi
    
    # Config directory
    CONFIG_DIR="$HOME/.config/proton-drive"
    if [ -d "$CONFIG_DIR" ]; then
        echo -e "${BLUE}  Removing $CONFIG_DIR${NC}"
        rm -rf "$CONFIG_DIR"
    fi
    
    # Cache directory
    CACHE_DIR="$HOME/.cache/proton-drive"
    if [ -d "$CACHE_DIR" ]; then
        echo -e "${BLUE}  Removing $CACHE_DIR${NC}"
        rm -rf "$CACHE_DIR"
    fi
    
    echo -e "${GREEN}[✓] User data removed${NC}"
}

# Function to remove sync setup
remove_sync_setup() {
    echo -e "${BLUE}[*] Removing sync setup...${NC}"
    
    # Stop and disable systemd service
    SERVICE_FILE="$HOME/.config/systemd/user/proton-drive-sync.service"
    if systemctl --user is-active proton-drive-sync.service &>/dev/null; then
        echo -e "${BLUE}  Stopping sync service...${NC}"
        systemctl --user stop proton-drive-sync.service 2>/dev/null || true
    fi
    
    if systemctl --user is-enabled proton-drive-sync.service &>/dev/null; then
        echo -e "${BLUE}  Disabling sync service...${NC}"
        systemctl --user disable proton-drive-sync.service 2>/dev/null || true
    fi
    
    if [ -f "$SERVICE_FILE" ]; then
        echo -e "${BLUE}  Removing systemd service file...${NC}"
        rm -f "$SERVICE_FILE"
        systemctl --user daemon-reload 2>/dev/null || true
    fi
    
    # Unmount any active rclone mounts
    MOUNT_POINT="$HOME/ProtonDrive"
    if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
        echo -e "${BLUE}  Unmounting $MOUNT_POINT...${NC}"
        fusermount -u "$MOUNT_POINT" 2>/dev/null || umount "$MOUNT_POINT" 2>/dev/null || true
    fi
    
    # Ask about rclone config
    echo -e "${YELLOW}  Do you want to remove rclone config (Proton Drive remote)? (y/n)${NC}"
    read -r remove_rclone_config
    if [[ "$remove_rclone_config" =~ ^[Yy]$ ]]; then
        if command -v rclone &> /dev/null; then
            echo -e "${BLUE}  Removing Proton Drive remote from rclone...${NC}"
            rclone config delete proton 2>/dev/null || true
        fi
    fi
    
    # Ask about rclone binary
    echo -e "${YELLOW}  Do you want to remove rclone itself? (y/n)${NC}"
    echo -e "${YELLOW}  (Only choose yes if you don't use rclone for other purposes)${NC}"
    read -r remove_rclone_binary
    if [[ "$remove_rclone_binary" =~ ^[Yy]$ ]]; then
        if command -v rclone &> /dev/null; then
            RCLONE_PATH=$(which rclone)
            echo -e "${BLUE}  Removing rclone from $RCLONE_PATH...${NC}"
            if [[ "$RCLONE_PATH" == "/usr/local/bin/rclone" ]]; then
                sudo rm -f /usr/local/bin/rclone /usr/local/share/man/man1/rclone.1
            else
                echo -e "${YELLOW}  rclone is managed by system package manager${NC}"
                echo -e "${YELLOW}  Run: sudo apt remove rclone (or equivalent)${NC}"
            fi
        fi
    fi
    
    echo -e "${GREEN}[✓] Sync setup removed${NC}"
}

# Execute based on user choice
case $choice in
    1)
        remove_application
        ;;
    2)
        remove_application
        remove_user_data
        ;;
    3)
        remove_application
        remove_sync_setup
        ;;
    4)
        remove_application
        remove_user_data
        remove_sync_setup
        ;;
esac

echo ""
echo -e "${GREEN}╔═══════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║     Uninstall Complete!                                    ║${NC}"
echo -e "${GREEN}╚═══════════════════════════════════════════════════════════╝${NC}"
echo ""

if [ "$choice" = "1" ]; then
    echo -e "${BLUE}User data remains in:${NC}"
    echo "  - $HOME/.local/share/proton-drive"
    echo "  - $HOME/.config/proton-drive"
    echo "  - $HOME/.cache/proton-drive"
    echo ""
fi

if [ "$choice" = "1" ] || [ "$choice" = "2" ]; then
    echo -e "${BLUE}Sync setup remains active.${NC}"
    echo "  Run this script again and choose option 3 or 4 to remove sync."
    echo ""
fi

echo -e "${BLUE}Thank you for using Proton Drive Linux!${NC}"
