#!/bin/bash
set -e

echo "🧪 Testing local build..."

# Run the standard build process
npm run build

# Success
echo "✅ Build test complete!"
echo "📦 Binaries in: src-native/build/proton-drive"
