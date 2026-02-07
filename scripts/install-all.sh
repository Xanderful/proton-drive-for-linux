#!/bin/bash
set -e

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${BLUE}╔═══════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║      Proton Drive Linux - Unified Installer               ║${NC}"
echo -e "${BLUE}╚═══════════════════════════════════════════════════════════╝${NC}"

# Check for dependencies
echo -e "${BLUE}[*] Checking dependencies...${NC}"
# Check for dependencies
echo -e "${BLUE}[*] Checking dependencies...${NC}"

# Define dependencies (Display Name -> PkgConfig Name/Binary)
declare -A DEPS
DEPS["cmake"]="cmake"
DEPS["g++"]="g++"
DEPS["pkg-config"]="pkg-config"
DEPS["webkit2gtk-4.1"]="webkit2gtk-4.1"
DEPS["libcurl"]="libcurl"
DEPS["libsoup-3.0"]="libsoup-3.0"
DEPS["ayatana-appindicator3"]="ayatana-appindicator3-0.1"
DEPS["sqlite3"]="sqlite3"
DEPS["node"]="node"
DEPS["npm"]="npm"
# Rclone and Fuse are checked later or via specific script

MISSING_DEPS=""

for name in "${!DEPS[@]}"; do
    check_target="${DEPS[$name]}"
    if ! pkg-config --exists "$check_target" 2>/dev/null && ! which "$check_target" > /dev/null 2>/dev/null; then
        # Try finding as binary if pkg-config failed (for tools)
        if ! which "$name" > /dev/null 2>/dev/null; then
             MISSING_DEPS="$MISSING_DEPS $name"
        fi
    fi
done

if [ ! -z "$MISSING_DEPS" ]; then
    echo -e "${YELLOW}[!] Potential missing dependencies: $MISSING_DEPS${NC}"
    
    if which apt > /dev/null 2>&1; then
        echo -e "${YELLOW}[!] Detected Debian/Ubuntu. Attempting to install...${NC}"
        sudo apt update || echo -e "${YELLOW}Warning: Some repositories failed to update, but continuing...${NC}"
        sudo apt install -y build-essential cmake pkg-config libwebkit2gtk-4.1-dev \
            libcurl4-openssl-dev libsoup-3.0-dev libayatana-appindicator3-dev \
            libsqlite3-dev rclone fuse3 nodejs npm
    elif which pacman > /dev/null 2>&1; then
        echo -e "${YELLOW}[!] Detected Arch Linux. Attempting to install...${NC}"
        sudo pacman -S --needed base-devel cmake webkit2gtk-4.1 curl libsoup3 libayatana-appindicator sqlite rclone fuse3 nodejs npm
    elif which dnf > /dev/null 2>&1; then
        echo -e "${YELLOW}[!] Detected Fedora/RHEL. Attempting to install...${NC}"
        sudo dnf install -y cmake gcc-c++ webkit2gtk4.1-devel libcurl-devel libsoup3-devel libayatana-appindicator-gtk3-devel sqlite-devel rclone fuse3 nodejs npm
    else
        echo -e "${RED}[!] Could not detect package manager. Please install missing dependencies manually.${NC}"
        echo -e "${RED}[!] Proceeding anyway (build might fail)...${NC}"
    fi
    echo ""
else
    echo -e "${GREEN}[✓] Build dependencies look good.${NC}"
fi

# 1. Build Native App
cwd=$(pwd)
echo -e "${BLUE}[*] Step 1/3: Building Native Client...${NC}"
mkdir -p src-native/build
cd src-native/build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
cd "$cwd"

# 2. Rclone Setup (Optional)
echo -e "${BLUE}[*] Step 2/3: Rclone Setup (Optional)...${NC}"
if [ -f "scripts/setup-rclone.sh" ]; then
    echo -e "${YELLOW}    Rclone setup script available at scripts/setup-rclone.sh${NC}"
    read -p "Run setup-rclone now? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        chmod +x scripts/setup-rclone.sh
        ./scripts/setup-rclone.sh
    fi
else
    echo -e "${YELLOW}[!] setup-rclone.sh not found${NC}"
fi

# 3. Desktop Entry
echo -e "${BLUE}[*] Step 3/3: Creating Desktop Entry...${NC}"
cat > ~/.local/share/applications/proton-drive-linux.desktop << EOL
[Desktop Entry]
Name=Proton Drive
Comment=Proton Drive Linux Client
Exec=$cwd/src-native/build/proton-drive
Icon=$cwd/src-native/resources/icons/proton-drive.svg
Terminal=false
Type=Application
Categories=Network;FileTransfer;
StartupNotify=true
EOL
chmod +x ~/.local/share/applications/proton-drive-linux.desktop

echo -e "${GREEN}╔═══════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║      Installation Complete!                               ║${NC}"
echo -e "${GREEN}╚═══════════════════════════════════════════════════════════╝${NC}"
echo -e "You can launch the app from your application menu or by running:"
echo -e "  $cwd/src-native/build/proton-drive"
