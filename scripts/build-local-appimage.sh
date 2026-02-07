#!/bin/bash
# Local AppImage build script for Proton Drive Linux (Native C++/GTK)
# Usage: ./scripts/build-local-appimage.sh [--skip-native]
#
# Creates a portable AppImage that works across Linux distributions.
# Requires: appimagetool, cmake, g++, and build dependencies
#
# AppImage capabilities:
#   - inotify file watching (kernel feature, no bundling needed)
#   - System tray via AppIndicator (bundled if available)
#   - File share access (standard filesystem access)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_ROOT"

# Parse arguments
SKIP_NATIVE=false
for arg in "$@"; do
    case "$arg" in
        --skip-native) SKIP_NATIVE=true ;;
    esac
done

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${BLUE}╔═══════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║      Proton Drive AppImage Build (Native C++)             ║${NC}"
echo -e "${BLUE}╚═══════════════════════════════════════════════════════════╝${NC}"

# Get version from package.json
VERSION=$(node -p "require('./package.json').version")
echo -e "${BLUE}Building version: ${GREEN}$VERSION${NC}"

# Define paths
APPDIR="$PROJECT_ROOT/dist/AppDir"
APPIMAGE_OUTPUT="$PROJECT_ROOT/dist"
APPIMAGE_NAME="ProtonDrive-${VERSION}-x86_64.AppImage"

# Check for appimagetool
if ! command -v appimagetool &> /dev/null; then
    echo -e "${YELLOW}[*] appimagetool not found, downloading...${NC}"
    mkdir -p "$PROJECT_ROOT/tools"
    wget -q "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage" \
        -O "$PROJECT_ROOT/tools/appimagetool"
    chmod +x "$PROJECT_ROOT/tools/appimagetool"
    # Extract if FUSE not available (common in containers)
    if ! "$PROJECT_ROOT/tools/appimagetool" --version &>/dev/null; then
        cd "$PROJECT_ROOT/tools"
        ./appimagetool --appimage-extract &>/dev/null || true
        mv squashfs-root appimagetool-extracted 2>/dev/null || true
        APPIMAGETOOL="$PROJECT_ROOT/tools/appimagetool-extracted/AppRun"
        cd "$PROJECT_ROOT"
    else
        APPIMAGETOOL="$PROJECT_ROOT/tools/appimagetool"
    fi
else
    APPIMAGETOOL="appimagetool"
fi

# Step 1: Build native binary
echo ""
echo -e "${BLUE}[1/5] Building Native Binary...${NC}"
if [ "$SKIP_NATIVE" = false ]; then
    mkdir -p src-native/build
    cd src-native/build
    cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
    make -j$(nproc)
    cd "$PROJECT_ROOT"
else
    echo -e "${YELLOW}  Skipping (--skip-native)${NC}"
    if [ ! -f "src-native/build/proton-drive" ]; then
        echo -e "${RED}ERROR: Native binary not found! Run without --skip-native first.${NC}"
        exit 1
    fi
fi

# Step 2: Create AppDir structure
echo ""
echo -e "${BLUE}[2/5] Creating AppDir structure...${NC}"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/lib"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/scalable/apps"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"
mkdir -p "$APPDIR/usr/share/proton-drive/scripts"

# Copy binary
cp src-native/build/proton-drive "$APPDIR/usr/bin/"

# Copy icons
cp src-native/resources/icons/proton-drive.svg "$APPDIR/usr/share/icons/hicolor/scalable/apps/"
cp src-native/resources/icons/proton-drive.svg "$APPDIR/proton-drive.svg"
if [ -f "src-native/resources/icons/proton-drive-tray.svg" ]; then
    cp src-native/resources/icons/proton-drive-tray.svg "$APPDIR/usr/share/icons/hicolor/scalable/apps/"
fi

# Convert SVG to PNG for compatibility (if rsvg-convert is available)
if command -v rsvg-convert &> /dev/null; then
    rsvg-convert -w 256 -h 256 src-native/resources/icons/proton-drive.svg \
        -o "$APPDIR/usr/share/icons/hicolor/256x256/apps/proton-drive.png" 2>/dev/null || true
fi

# Copy helper scripts for rclone sync (optional but useful)
if [ -f "scripts/setup-rclone-from-app.sh" ]; then
    cp scripts/setup-rclone-from-app.sh "$APPDIR/usr/share/proton-drive/scripts/"
fi
if [ -f "scripts/manage-sync-job.sh" ]; then
    cp scripts/manage-sync-job.sh "$APPDIR/usr/share/proton-drive/scripts/"
fi

# Copy .desktop file from packaging
cp src-native/packaging/proton-drive.desktop "$APPDIR/usr/share/applications/proton-drive.desktop"

# Symlink desktop and icon to AppDir root (required by AppImage)
ln -sf usr/share/applications/proton-drive.desktop "$APPDIR/proton-drive.desktop"

# Step 3: Create AppRun
echo ""
echo -e "${BLUE}[3/5] Creating AppRun entry point...${NC}"
cat > "$APPDIR/AppRun" << 'APPRUN_EOF'
#!/bin/bash
# AppRun for Proton Drive Linux
# Handles environment setup for portable execution with full isolation
# from snap, flatpak, and other containerized environment pollution

SELF=$(readlink -f "$0")
HERE=${SELF%/*}

# ============================================================================
# CRITICAL: Clean environment from snap/flatpak library pollution
# ============================================================================
# When launched from snap-based apps (like VS Code snap), environment variables
# can leak in that point to /snap/core20/... libraries which conflict with 
# system GTK4 causing "symbol lookup error: undefined symbol: __libc_pthread_init"
# 
# This section removes ALL potentially conflicting paths before we set up ours
# ============================================================================

# Helper function to filter out snap/flatpak paths from PATH-like variables
clean_path_var() {
    local var_name="$1"
    local current_val="${!var_name:-}"
    local cleaned=""
    local IFS=':'
    
    for path in $current_val; do
        # Skip snap, flatpak, and container paths
        case "$path" in
            /snap/*|*/snap/*|/var/lib/flatpak/*|/var/lib/snapd/*|*/.local/share/flatpak/*)
                # Skip these contaminated paths
                ;;
            *)
                if [ -n "$cleaned" ]; then
                    cleaned="$cleaned:$path"
                else
                    cleaned="$path"
                fi
                ;;
        esac
    done
    
    echo "$cleaned"
}

# Clean LD_LIBRARY_PATH - this is the main culprit for snap library conflicts
if [ -n "${LD_LIBRARY_PATH:-}" ]; then
    export LD_LIBRARY_PATH="$(clean_path_var LD_LIBRARY_PATH)"
fi

# Also clean LD_PRELOAD which can load incompatible libraries
if [ -n "${LD_PRELOAD:-}" ]; then
    # Completely unset if it contains snap paths
    case "${LD_PRELOAD:-}" in
        *snap*|*flatpak*)
            unset LD_PRELOAD
            ;;
    esac
fi

# Clean library-related environment variables that snaps might pollute
unset SNAP
unset SNAP_NAME
unset SNAP_INSTANCE_NAME
unset SNAP_ARCH
unset SNAP_COMMON
unset SNAP_DATA
unset SNAP_LIBRARY_PATH
unset SNAP_USER_COMMON
unset SNAP_USER_DATA
unset SNAP_LAUNCHER_ARCH_TRIPLET

# Clean GTK module paths that might point to incompatible snap modules
if [ -n "${GTK_PATH:-}" ]; then
    export GTK_PATH="$(clean_path_var GTK_PATH)"
fi

if [ -n "${GTK_EXE_PREFIX:-}" ]; then
    case "${GTK_EXE_PREFIX:-}" in
        *snap*|*flatpak*)
            unset GTK_EXE_PREFIX
            ;;
    esac
fi

# Clean GI (GObject Introspection) paths
if [ -n "${GI_TYPELIB_PATH:-}" ]; then
    export GI_TYPELIB_PATH="$(clean_path_var GI_TYPELIB_PATH)"
fi

# Clean GIO module paths
if [ -n "${GIO_MODULE_DIR:-}" ]; then
    case "${GIO_MODULE_DIR:-}" in
        *snap*|*flatpak*)
            unset GIO_MODULE_DIR
            ;;
    esac
fi

# Clean GST (GStreamer) paths
if [ -n "${GST_PLUGIN_PATH:-}" ]; then
    export GST_PLUGIN_PATH="$(clean_path_var GST_PLUGIN_PATH)"
fi
if [ -n "${GST_PLUGIN_SYSTEM_PATH:-}" ]; then
    export GST_PLUGIN_SYSTEM_PATH="$(clean_path_var GST_PLUGIN_SYSTEM_PATH)"
fi

# Clean XDG paths from snap contamination
if [ -n "${XDG_DATA_DIRS:-}" ]; then
    export XDG_DATA_DIRS="$(clean_path_var XDG_DATA_DIRS)"
fi

# Clean PYTHONPATH and other language paths
if [ -n "${PYTHONPATH:-}" ]; then
    export PYTHONPATH="$(clean_path_var PYTHONPATH)"
fi

# ============================================================================
# Set up clean application environment
# ============================================================================

export PATH="${HERE}/usr/bin:${PATH}"

# Set up library path for bundled libraries (if any) - use ONLY our paths first
if [ -d "${HERE}/usr/lib" ]; then
    if [ -n "${LD_LIBRARY_PATH:-}" ]; then
        export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH}"
    else
        export LD_LIBRARY_PATH="${HERE}/usr/lib"
    fi
fi

# Set XDG paths for proper desktop integration (with system fallbacks)
if [ -z "${XDG_DATA_DIRS:-}" ]; then
    export XDG_DATA_DIRS="${HERE}/usr/share:/usr/local/share:/usr/share"
else
    export XDG_DATA_DIRS="${HERE}/usr/share:${XDG_DATA_DIRS}"
fi

# Icon path for system tray (AppIndicator) - read by tray_gtk4.cpp
export PROTON_DRIVE_ICON_PATH="${HERE}/usr/share/icons/hicolor/scalable/apps"

# Ensure data directory is in user's real home, not snap home
if [ -n "${SNAP_USER_COMMON:-}" ] || [[ "${HOME:-}" == *"/snap/"* ]]; then
    # We're being launched from a snap context, find the real home
    REAL_HOME=$(getent passwd "$(id -un)" | cut -d: -f6)
    export HOME="${REAL_HOME}"
    export XDG_CONFIG_HOME="${REAL_HOME}/.config"
    export XDG_DATA_HOME="${REAL_HOME}/.local/share"
    export XDG_CACHE_HOME="${REAL_HOME}/.cache"
fi

# Ensure inotify limits are adequate (warn if low)
MAX_WATCHES=$(cat /proc/sys/fs/inotify/max_user_watches 2>/dev/null || echo "0")
if [ "$MAX_WATCHES" -lt 65536 ]; then
    echo "Warning: inotify watch limit is low ($MAX_WATCHES). For better sync performance:"
    echo "  sudo sysctl fs.inotify.max_user_watches=524288"
fi

# GDK/GTK backend settings for compatibility
export GDK_BACKEND="${GDK_BACKEND:-x11,wayland}"

# GTK4 backend settings for AppImage portability
if [ -n "$APPIMAGE" ]; then
    export GDK_BACKEND="${GDK_BACKEND:-x11,wayland}"
fi

# Run the application
exec "${HERE}/usr/bin/proton-drive" "$@"
APPRUN_EOF
chmod +x "$APPDIR/AppRun"

# Step 4: Bundle rclone binary
echo ""
echo -e "${BLUE}[4/5] Bundling rclone with Proton Drive upload fix...${NC}"

# Build rclone from coderFrankenstain's fork with PR #9081 fix
# (Official release doesn't have the block verification fix yet)
RCLONE_TEMP="/tmp/rclone-appimage-$$"
RCLONE_VERSION="v1.73.0-DEV+protondrive-fix"

mkdir -p "$RCLONE_TEMP"
cd "$RCLONE_TEMP"

echo -e "  Checking for Go compiler..."
if ! command -v go &> /dev/null; then
    echo -e "${RED}Error: Go compiler not found. Install with:${NC}"
    echo -e "${YELLOW}  sudo apt install golang-go   # Ubuntu/Debian${NC}"
    echo -e "${YELLOW}  sudo pacman -S go            # Arch${NC}"
    exit 1
fi

echo -e "  Cloning rclone with Proton Drive upload fix (PR #9081)..."
if ! git clone --depth 1 -b fix-protondrive-upload https://github.com/coderFrankenstain/rclone.git rclone-fix 2>&1; then
    echo -e "${RED}Error: Failed to clone rclone repository${NC}"
    echo -e "${YELLOW}This might be due to network issues or the branch no longer existing${NC}"
    echo -e "${YELLOW}Falling back to system rclone if available${NC}"
    
    if command -v rclone &> /dev/null; then
        echo -e "${YELLOW}Using system rclone instead${NC}"
        SYSTEM_RCLONE=$(which rclone)
        cp "$SYSTEM_RCLONE" "$APPDIR/usr/bin/rclone"
        chmod +x "$APPDIR/usr/bin/rclone"
        cd "$PROJECT_ROOT"
        rm -rf "$RCLONE_TEMP"
        echo -e "${YELLOW}⚠️${NC} Using system rclone (may not have Proton Drive upload fix)"
    else
        echo -e "${RED}Error: No rclone available. Install rclone or fix network connection.${NC}"
        cd "$PROJECT_ROOT"
        rm -rf "$RCLONE_TEMP"
        exit 1
    fi
else
    cd rclone-fix
    
    echo -e "  Building rclone from source..."
    if ! CGO_ENABLED=0 go build -o rclone-fixed 2>&1; then
        echo -e "${RED}Error: rclone build failed${NC}"
        cd "$PROJECT_ROOT"
        rm -rf "$RCLONE_TEMP"
        exit 1
    fi
    
    if [ -f rclone-fixed ]; then
        cp rclone-fixed "$APPDIR/usr/bin/rclone"
        chmod +x "$APPDIR/usr/bin/rclone"
        echo -e "  ${GREEN}✓${NC} rclone with Proton Drive upload fix bundled"
        
        # Verify rclone works
        "$APPDIR/usr/bin/rclone" version | head -n 1
    else
        echo -e "${RED}Error: rclone binary not found after build${NC}"
        cd "$PROJECT_ROOT"
        rm -rf "$RCLONE_TEMP"
        exit 1
    fi
    
    # Cleanup
    cd "$PROJECT_ROOT"
    rm -rf "$RCLONE_TEMP"
fi

# Step 5: Bundle required libraries (optional, for maximum portability)
echo ""
echo -e "${BLUE}[5/5] Bundling libraries for portability...${NC}"

# Function to copy a library and its dependencies
bundle_library() {
    local lib="$1"
    local dest="$APPDIR/usr/lib"
    
    if [ -f "$lib" ] && [ ! -f "$dest/$(basename "$lib")" ]; then
        cp "$lib" "$dest/" 2>/dev/null || true
        echo -e "  ${GREEN}✓${NC} $(basename "$lib")"
    fi
}

# Note: GTK4 is not bundled (too large and system-dependent)
# The host system must have GTK4 installed

echo -e "${YELLOW}  Note: GTK4 is NOT bundled (uses system libraries)${NC}"
echo -e "${YELLOW}  The AppImage requires these system packages:${NC}"
echo -e "${YELLOW}    - Debian/Ubuntu: libgtk-4-1, libcurl4, libsqlite3-0${NC}"
echo -e "${YELLOW}    - Fedora: gtk4, libcurl, sqlite${NC}"
echo -e "${YELLOW}    - Arch: gtk4, curl, sqlite${NC}"

# Create the AppImage
echo ""
echo -e "${BLUE}Building AppImage...${NC}"
mkdir -p "$APPIMAGE_OUTPUT"

# Set architecture
export ARCH=x86_64

# Build the AppImage
cd "$PROJECT_ROOT"
"$APPIMAGETOOL" "$APPDIR" "$APPIMAGE_OUTPUT/$APPIMAGE_NAME"

# Make executable
chmod +x "$APPIMAGE_OUTPUT/$APPIMAGE_NAME"

echo ""
echo -e "${GREEN}╔═══════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║      AppImage Build Complete!                             ║${NC}"
echo -e "${GREEN}╚═══════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "Output: ${BLUE}$APPIMAGE_OUTPUT/$APPIMAGE_NAME${NC}"
ls -lh "$APPIMAGE_OUTPUT/$APPIMAGE_NAME"
echo ""
echo -e "${YELLOW}System Requirements:${NC}"
echo -e "  • Linux kernel 2.6.13+ (for inotify - virtually all modern systems)"
echo -e "  • GTK 4.0+ (libgtk-4-1)"
echo -e "  • libcurl (usually pre-installed)"
echo -e "  • AppIndicator library (for system tray - optional)"
echo -e "  • FUSE (for rclone mount - optional, only needed for folder sync)"
echo ""
echo -e "${YELLOW}To run:${NC}"
echo -e "  chmod +x $APPIMAGE_NAME"
echo -e "  ./$APPIMAGE_NAME"
echo ""
echo -e "${YELLOW}For better inotify performance (large directories):${NC}"
echo -e "  sudo sysctl -w fs.inotify.max_user_watches=524288"
echo -e "  echo 'fs.inotify.max_user_watches=524288' | sudo tee -a /etc/sysctl.conf"
