#!/bin/bash
# Release helper script for Proton Drive Linux
# Usage: ./scripts/create-release.sh <version>
# Example: ./scripts/create-release.sh 1.2.3

set -euo pipefail

if [ $# -eq 0 ]; then
    echo "Usage: $0 <version>"
    echo "Example: $0 1.2.3"
    exit 1
fi

VERSION="$1"
VERSION_TAG="v${VERSION}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_ROOT"

echo "Creating release for version $VERSION"
echo "======================================="
echo ""

# 1. Check if working directory is clean
if ! git diff-index --quiet HEAD --; then
    echo "❌ Error: Working directory is not clean. Commit or stash changes first."
    exit 1
fi

# 2. Update version in package.json
echo "📝 Updating version in package.json..."
if command -v jq &> /dev/null; then
    tmp=$(mktemp)
    jq ".version = \"$VERSION\"" package.json > "$tmp" && mv "$tmp" package.json
else
    # Fallback: use sed
    sed -i "s/\"version\": \".*\"/\"version\": \"$VERSION\"/" package.json
fi

# 3. Update CHANGELOG.md (if it exists)
if [ -f "CHANGELOG.md" ]; then
    echo "📝 Adding release entry to CHANGELOG.md..."
    DATE=$(date +%Y-%m-%d)
    
    # Check if version already exists in changelog
    if ! grep -q "## \[$VERSION\]" CHANGELOG.md; then
        # Add new version entry after the header
        sed -i "/^# Changelog/a\\
\\
## [$VERSION] - $DATE\\
\\
### Added\\
- (TODO: Add new features)\\
\\
### Changed\\
- (TODO: Add changes)\\
\\
### Fixed\\
- (TODO: Add bug fixes)\\
" CHANGELOG.md
        
        echo "⚠️  Please edit CHANGELOG.md to add release notes"
        ${EDITOR:-nano} CHANGELOG.md
    fi
fi

# 4. Commit version bump
echo ""
echo "💾 Committing version bump..."
git add package.json CHANGELOG.md
git commit -m "Release $VERSION_TAG"

# 5. Create and push tag
echo ""
echo "🏷️  Creating tag $VERSION_TAG..."
git tag -a "$VERSION_TAG" -m "Release $VERSION_TAG"

echo ""
echo "✅ Release prepared!"
echo ""
echo "Next steps:"
echo "  1. Review the changes: git show HEAD"
echo "  2. Push to GitHub: git push origin main && git push origin $VERSION_TAG"
echo ""
echo "The GitHub Actions workflow will automatically:"
echo "  - Build AppImage (and other formats if configured)"
echo "  - Create GitHub release"
echo "  - Upload build artifacts"
echo ""
echo "To undo: git reset HEAD~1 && git tag -d $VERSION_TAG"
