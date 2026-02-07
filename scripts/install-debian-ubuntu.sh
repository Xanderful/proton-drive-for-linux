#!/bin/bash
# Proton Drive Linux - Debian/Ubuntu Installation Script
# Compatible with: Debian 11+, Ubuntu 20.04+, Linux Mint 20+, Pop!_OS, and derivatives
#
# This script installs all dependencies and builds the application.
# For AppImage builds, use: ./scripts/build-local-appimage.sh

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${BLUE}╔═══════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║     Proton Drive Linux - Debian/Ubuntu Installer          ║${NC}"
echo -e "${BLUE}╚═══════════════════════════════════════════════════════════╝${NC}"
echo ""

# Check if running as root
if [ "$EUID" -eq 0 ]; then
    echo -e "${YELLOW}Warning: Running as root. Consider running as normal user with sudo access.${NC}"
fi

# Detect distribution
if [ -f /etc/os-release ]; then
    . /etc/os-release
    DISTRO="$ID"
    VERSION="$VERSION_ID"
    echo -e "${BLUE}Detected: ${GREEN}$PRETTY_NAME${NC}"
else
    DISTRO="unknown"
    VERSION="0"
    echo -e "${YELLOW}Could not detect distribution. Assuming Debian-based.${NC}"
fi

# Check if apt is available
if ! command -v apt &> /dev/null; then
    echo -e "${RED}Error: apt package manager not found. This script is for Debian/Ubuntu systems.${NC}"
    exit 1
fi

# Function to install packages
install_packages() {
    echo -e "${BLUE}[*] Installing system dependencies...${NC}"
    
    # Update package lists (allow to fail if some repos have issues)
    echo -e "${BLUE}  Updating package lists...${NC}"
    sudo apt update || echo -e "${YELLOW}Warning: Some repositories failed to update, but continuing...${NC}"
    
    # Core build tools
    echo -e "${BLUE}  Installing build tools...${NC}"
    sudo apt install -y \
        build-essential \
        cmake \
        pkg-config \
        git \
        wget \
        curl
    
    # WebKitGTK - try GTK4 version first, fall back to GTK3
    echo -e "${BLUE}  Installing WebKitGTK...${NC}"
    if apt-cache show libwebkitgtk-6.0-dev &>/dev/null; then
        echo -e "${GREEN}    Using WebKitGTK 6.0 (GTK4)${NC}"
        sudo apt install -y libwebkitgtk-6.0-dev libgtk-4-dev
    elif apt-cache show libwebkit2gtk-4.1-dev &>/dev/null; then
        echo -e "${YELLOW}    WebKitGTK 6.0 not available, using 4.1 (GTK3)${NC}"
        sudo apt install -y libwebkit2gtk-4.1-dev libgtk-3-dev
    else
        echo -e "${YELLOW}    WebKitGTK 4.1 not available, trying 4.0${NC}"
        sudo apt install -y libwebkit2gtk-4.0-dev libgtk-3-dev
    fi
    
    # Other dependencies
    echo -e "${BLUE}  Installing additional libraries...${NC}"
    sudo apt install -y \
        libcurl4-openssl-dev \
        libsoup-3.0-dev \
        libayatana-appindicator3-dev \
        libsqlite3-dev \
        libssl-dev \
        librsvg2-bin
    
    # Node.js - check if already installed
    if command -v node &> /dev/null && [ "$(node -v | cut -d'v' -f2 | cut -d'.' -f1)" -ge 18 ]; then
        echo -e "${GREEN}  Node.js $(node -v) already installed${NC}"
    else
        echo -e "${BLUE}  Installing Node.js 20 LTS...${NC}"
        # Use NodeSource for up-to-date Node.js
        if [ ! -f /etc/apt/sources.list.d/nodesource.list ]; then
            curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
        fi
        sudo apt install -y nodejs
    fi
    
    # Optional: rclone and FUSE for folder sync
    echo -e "${BLUE}  Installing rclone and FUSE (for folder sync)...${NC}"
    sudo apt install -y fuse3 || sudo apt install -y fuse
    
    # Install rclone (use official installer for latest version with Proton Drive support)
    if ! command -v rclone &> /dev/null || ! rclone version | grep -q "v1.6"; then
        echo -e "${YELLOW}    Installing/updating rclone (requires v1.64+ for Proton Drive)...${NC}"
        curl https://rclone.org/install.sh | sudo bash || true
    else
        echo -e "${GREEN}    rclone $(rclone version | head -1 | awk '{print $2}') already installed${NC}"
    fi
    
    echo -e "${GREEN}[✓] Dependencies installed successfully${NC}"
}

# Function to increase inotify limits
configure_inotify() {
    echo -e "${BLUE}[*] Configuring inotify limits for file watching...${NC}"
    
    CURRENT_LIMIT=$(cat /proc/sys/fs/inotify/max_user_watches)
    RECOMMENDED_LIMIT=524288
    
    if [ "$CURRENT_LIMIT" -lt "$RECOMMENDED_LIMIT" ]; then
        echo -e "${YELLOW}  Current inotify limit: $CURRENT_LIMIT (low)${NC}"
        echo -e "${BLUE}  Setting to $RECOMMENDED_LIMIT for better sync performance...${NC}"
        
        # Set immediately
        echo "$RECOMMENDED_LIMIT" | sudo tee /proc/sys/fs/inotify/max_user_watches > /dev/null
        
        # Make persistent
        if ! grep -q "fs.inotify.max_user_watches" /etc/sysctl.conf; then
            echo "fs.inotify.max_user_watches=$RECOMMENDED_LIMIT" | sudo tee -a /etc/sysctl.conf > /dev/null
            echo -e "${GREEN}  Added to /etc/sysctl.conf for persistence${NC}"
        fi
        
        echo -e "${GREEN}[✓] inotify limits configured${NC}"
    else
        echo -e "${GREEN}  inotify limit already adequate: $CURRENT_LIMIT${NC}"
    fi
}

# Function to build the application
build_app() {
    echo -e "${BLUE}[*] Building Proton Drive Linux...${NC}"
    
    # Check if we're in the right directory
    if [ ! -f "package.json" ] || [ ! -d "src-native" ]; then
        echo -e "${RED}Error: Please run this script from the project root directory.${NC}"
        exit 1
    fi
    
    # Initialize/update submodules
    echo -e "${BLUE}  Initializing git submodules...${NC}"
    git submodule update --init --recursive || true
    
    # Build WebClients
    echo -e "${BLUE}  Building WebClients (this may take a while)...${NC}"
    if [ -f "scripts/build-webclients.sh" ]; then
        bash scripts/build-webclients.sh
    else
        echo -e "${YELLOW}  build-webclients.sh not found, trying direct build...${NC}"
        cd WebClients
        npm install
        npm run build:web -w proton-drive
        cd ..
    fi
    
    # Build native binary
    echo -e "${BLUE}  Building native client...${NC}"
    mkdir -p src-native/build
    cd src-native/build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j$(nproc)
    cd ../..
    
    echo -e "${GREEN}[✓] Build completed successfully${NC}"
}

# Function to create desktop entry
create_desktop_entry() {
    echo -e "${BLUE}[*] Creating desktop entry...${NC}"
    
    PROJECT_DIR=$(pwd)
    
    mkdir -p ~/.local/share/applications
    mkdir -p ~/.local/share/icons/hicolor/scalable/apps
    
    # Copy icon
    cp src-native/resources/icons/proton-drive.svg ~/.local/share/icons/hicolor/scalable/apps/
    
    # Create desktop file
    cat > ~/.local/share/applications/proton-drive.desktop << EOF
[Desktop Entry]
Name=Proton Drive
Comment=Encrypted cloud storage with two-way sync
Exec=${PROJECT_DIR}/src-native/build/proton-drive
Icon=proton-drive
Terminal=false
Type=Application
Categories=Network;FileTransfer;Utility;
Keywords=proton;drive;cloud;storage;encryption;sync;
StartupNotify=true
StartupWMClass=proton-drive
EOF
    
    chmod +x ~/.local/share/applications/proton-drive.desktop
    
    # Update icon cache
    gtk-update-icon-cache ~/.local/share/icons/hicolor 2>/dev/null || true
    
    echo -e "${GREEN}[✓] Desktop entry created${NC}"
}

# Function to install system-wide
install_system() {
    echo -e "${BLUE}[*] Installing system-wide...${NC}"
    
    # Check if built
    if [ ! -f "src-native/build/proton-drive" ]; then
        echo -e "${RED}Error: Binary not found. Run build first.${NC}"
        exit 1
    fi
    
    # Install binary
    sudo install -Dm755 src-native/build/proton-drive /usr/local/bin/proton-drive
    
    # Install icons
    sudo install -Dm644 src-native/resources/icons/proton-drive.svg \
        /usr/local/share/icons/hicolor/scalable/apps/proton-drive.svg
    if [ -f "src-native/resources/icons/proton-drive-tray.svg" ]; then
        sudo install -Dm644 src-native/resources/icons/proton-drive-tray.svg \
            /usr/local/share/icons/hicolor/scalable/apps/proton-drive-tray.svg
    fi
    
    # Install WebClients
    sudo mkdir -p /usr/local/share/proton-drive/webclients
    if [ -d "WebClients/applications/drive/dist" ]; then
        sudo cp -r WebClients/applications/drive/dist/* /usr/local/share/proton-drive/webclients/
    fi
    
    # Install desktop file
    sudo install -Dm644 src-native/packaging/proton-drive.desktop \
        /usr/local/share/applications/proton-drive.desktop
    
    # Update desktop database
    sudo update-desktop-database /usr/local/share/applications 2>/dev/null || true
    sudo gtk-update-icon-cache /usr/local/share/icons/hicolor 2>/dev/null || true
    
    echo -e "${GREEN}[✓] Installed to /usr/local${NC}"
}

# Main menu
show_menu() {
    echo ""
    echo -e "${BLUE}What would you like to do?${NC}"
    echo "  1) Install dependencies only"
    echo "  2) Full installation (deps + build + desktop entry)"
    echo "  3) Build only (assumes deps are installed)"
    echo "  4) System-wide install (after build)"
    echo "  5) Build AppImage"
    echo "  6) Configure inotify limits"
    echo "  7) Exit"
    echo ""
    read -p "Choose an option [1-7]: " choice
    
    case $choice in
        1)
            install_packages
            ;;
        2)
            install_packages
            configure_inotify
            build_app
            create_desktop_entry
            echo ""
            echo -e "${GREEN}╔═══════════════════════════════════════════════════════════╗${NC}"
            echo -e "${GREEN}║     Installation Complete!                                ║${NC}"
            echo -e "${GREEN}╚═══════════════════════════════════════════════════════════╝${NC}"
            echo ""
            echo -e "You can now launch Proton Drive from your application menu,"
            echo -e "or run: ${BLUE}./src-native/build/proton-drive${NC}"
            echo ""
            echo -e "${BLUE}To uninstall later, run: ${NC}${GREEN}make uninstall${NC} or ${GREEN}./scripts/uninstall.sh${NC}"
            ;;
        3)
            build_app
            create_desktop_entry
            ;;
        4)
            install_system
            ;;
        5)
            install_packages
            bash scripts/build-local-appimage.sh
            ;;
        6)
            configure_inotify
            ;;
        7)
            echo "Goodbye!"
            exit 0
            ;;
        *)
            echo -e "${RED}Invalid option${NC}"
            show_menu
            ;;
    esac
}

# Parse command line arguments
case "${1:-}" in
    --deps-only)
        install_packages
        ;;
    --build-only)
        build_app
        ;;
    --full)
        install_packages
        configure_inotify
        build_app
        create_desktop_entry
        ;;
    --appimage)
        install_packages
        bash scripts/build-local-appimage.sh
        ;;
    --system-install)
        install_system
        ;;
    --help|-h)
        echo "Usage: $0 [option]"
        echo ""
        echo "Options:"
        echo "  --deps-only      Install dependencies only"
        echo "  --build-only     Build the application (deps must be installed)"
        echo "  --full           Full installation (deps + build + desktop entry)"
        echo "  --appimage       Build an AppImage"
        echo "  --system-install Install system-wide (after build)"
        echo "  --help           Show this help message"
        echo ""
        echo "Run without arguments for interactive menu."
        ;;
    *)
        show_menu
        ;;
esac
