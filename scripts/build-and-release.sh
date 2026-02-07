#!/bin/bash
set -e

echo "🏗️  Building Proton Drive Linux (Native GTK4)..."

# Build native C++ app
echo "🔨 Building native application..."
mkdir -p src-native/build
cd src-native/build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
cd ../..

echo ""
echo "✅ Build complete!"
echo ""
echo "📁 Binary location:"
echo "   src-native/build/proton-drive"
echo ""
echo "To run:"
echo "   ./src-native/build/proton-drive"
echo ""
echo "To install system-wide (optional):"
echo "   sudo cp src-native/build/proton-drive /usr/local/bin/"
echo "   sudo cp src-native/packaging/proton-drive.desktop /usr/share/applications/"
