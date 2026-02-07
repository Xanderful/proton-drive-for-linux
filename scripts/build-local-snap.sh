#!/bin/bash
set -e

# Proton Drive Linux - Snap Builder
# Builds the native GTK4 application for Snap

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"

echo "📦 Proton Drive Linux - Snap Builder"
echo "===================================="

# Get version from package.json
VERSION=$(node -p "require('./package.json').version" 2>/dev/null || echo "1.2.0")
echo "Building version: $VERSION"

# Step 1: Build native binary
echo ""
echo "📋 Step 1/3: Building native application..."
mkdir -p src-native/build
cd src-native/build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
cd "$PROJECT_ROOT"
echo "✅ Native binary built"

# Step 2: Create snap structure
echo ""
echo "📋 Step 2/3: Creating snap structure..."
mkdir -p snap

# Generate snapcraft.yaml
cat > snap/snapcraft.yaml << EOF
name: proton-drive
version: "$VERSION"
summary: Proton Drive - Native GTK4 Desktop Client
description: |
  Fast and lightweight Proton Drive client for Linux.
  Built with pure C++ and GTK4 for native performance.
  Features drag-drop file sync, cloud browsing, and real-time folder synchronization.
grade: stable
confinement: strict
base: core24
platforms:
  amd64:

apps:
  proton-drive:
    command: usr/bin/proton-drive
    desktop: usr/share/applications/proton-drive.desktop
    plugs:
      - home
      - network
      - network-bind
      - desktop
      - x11
      - wayland
      - opengl

parts:
  proton-drive:
    plugin: nil
    source: .
    override-build: |
      # Copy pre-built binary
      install -Dm755 src-native/build/proton-drive "\$SNAPCRAFT_PART_INSTALL/usr/bin/proton-drive"

      # Desktop file
      mkdir -p "\$SNAPCRAFT_PART_INSTALL/usr/share/applications"
      install -m644 src-native/packaging/proton-drive.desktop "\$SNAPCRAFT_PART_INSTALL/usr/share/applications/"

      # Icons
      mkdir -p "\$SNAPCRAFT_PART_INSTALL/usr/share/icons/hicolor/scalable/apps"
      if [ -f "src-native/resources/icons/proton-drive.svg" ]; then
        install -m644 src-native/resources/icons/proton-drive.svg "\$SNAPCRAFT_PART_INSTALL/usr/share/icons/hicolor/scalable/apps/"
      fi

    stage-packages:
      - libgtk-4-1
      - libssl3
      - libcurl4
      - libsqlite3-0
      - rclone
      - fuse3
EOF

echo "✅ Snap structure created"

# Step 3: Build snap
echo ""
echo "📋 Step 3/3: Building snap package..."
snapcraft --destructive-mode

echo ""
echo "=========================================="
echo "✅ Snap Build Complete!"
echo "=========================================="
echo ""
echo "📦 Output: $(ls -1 proton-drive_*.snap 2>/dev/null | head -1)"
echo ""
echo "To install:"
echo "  sudo snap install --dangerous proton-drive_${VERSION}_amd64.snap"
echo ""
