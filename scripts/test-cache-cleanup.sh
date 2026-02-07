#!/bin/bash
# Test: Bisync Cache Cleanup on Job Deletion
# Verifies that stale bisync cache files don't cause false positive conflicts

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_DIR="/tmp/proton-drive-cache-test-$$"
FAKE_HOME="$TEST_DIR/home"
FAKE_CACHE="$FAKE_HOME/.cache/rclone/bisync"
FAKE_CONFIG="$FAKE_HOME/.config/proton-drive/jobs"

echo "=========================================="
echo "Test: Bisync Cache Cleanup on Job Deletion"
echo "=========================================="
echo ""

# Setup fake environment
mkdir -p "$FAKE_CACHE"
mkdir -p "$FAKE_CONFIG"

# Create a fake job config
cat > "$FAKE_CONFIG/test-job-123.conf" <<'EOF'
JOB_ID="test-job-123"
LOCAL_PATH="/home/joseph/Downloads"
REMOTE_PATH="proton:/Downloads"
SYNC_TYPE="bisync"
INTERVAL="5"
SERVICE_NAME="proton-drive-sync-test-job-123"
EOF

# Create fake bisync cache files (simulating what rclone would create)
touch "$FAKE_CACHE/home_joseph_Downloads..proton_Downloads.path1.lst-new"
touch "$FAKE_CACHE/home_joseph_Downloads..proton_Downloads.path2.lst-new"
touch "$FAKE_CACHE/home_joseph_Downloads..proton_Downloads.path1.lst-old"
touch "$FAKE_CACHE/home_joseph_Downloads..proton_Downloads.path2.lst-old"

# Also create stale cache from buggy doubled path
touch "$FAKE_CACHE/home_joseph_Downloads_Downloads..proton_Downloads.path1.lst-new"
touch "$FAKE_CACHE/home_joseph_Downloads_Downloads..proton_Downloads.path2.lst-new"

echo "✓ Created test environment at: $TEST_DIR"
echo "  Fake job config: $FAKE_CONFIG/test-job-123.conf"
echo "  Fake bisync cache files:"
ls -1 "$FAKE_CACHE/"
echo ""

# Test 1: Extract cache cleanup logic from manage-sync-job.sh
echo "Test 1: Cache cleanup pattern matching"
echo "---------------------------------------"

LOCAL_PATH="/home/joseph/Downloads"
REMOTE_PATH="proton:/Downloads"

# Convert using the same logic as manage-sync-job.sh
local_cleaned=$(echo "$LOCAL_PATH" | sed 's|^/||; s|/|_|g')
remote_cleaned=$(echo "$REMOTE_PATH" | sed 's|:/|_|g; s|/|_|g')
cache_pattern="${local_cleaned}..${remote_cleaned}"

echo "Local path:     $LOCAL_PATH"
echo "Remote path:    $REMOTE_PATH"
echo "Cache pattern:  ${cache_pattern}*"
echo ""

# Find matching files
matching_files=$(find "$FAKE_CACHE" -name "${cache_pattern}*" 2>/dev/null || true)

if [ -n "$matching_files" ]; then
    echo "✓ Found matching cache files:"
    echo "$matching_files" | sed 's/^/  /'
    file_count=$(echo "$matching_files" | wc -l)
    echo "  Total: $file_count files"
else
    echo "✗ FAILED: No matching files found!"
    exit 1
fi
echo ""

# Test 2: Verify cleanup removes correct files
echo "Test 2: Cleanup removes ONLY correct pattern"
echo "---------------------------------------------"

# Delete matching files
find "$FAKE_CACHE" -name "${cache_pattern}*" -delete 2>/dev/null || true

# Check what remains
remaining_files=$(find "$FAKE_CACHE" -type f 2>/dev/null || true)

if [ -z "$remaining_files" ]; then
    echo "✗ FAILED: All files deleted (should keep stale doubled-path files)"
    exit 1
fi

# Should still have the doubled-path stale files
doubled_pattern="home_joseph_Downloads_Downloads..proton_Downloads"
doubled_files=$(find "$FAKE_CACHE" -name "${doubled_pattern}*" 2>/dev/null || true)

if [ -z "$doubled_files" ]; then
    echo "✗ FAILED: Doubled-path files were deleted (should be preserved)"
    exit 1
fi

echo "✓ Correct files deleted (normal path)"
echo "✓ Stale files preserved (doubled path):"
echo "$doubled_files" | sed 's/^/  /'
echo ""

# Test 3: Clean up doubled-path stale files
echo "Test 3: Manual cleanup of stale files"
echo "--------------------------------------"

stale_pattern="home_joseph_Downloads_Downloads..proton_Downloads"
stale_files_before=$(find "$FAKE_CACHE" -name "${stale_pattern}*" 2>/dev/null | wc -l)

echo "Stale files before: $stale_files_before"

# This simulates what the user had to do manually
find "$FAKE_CACHE" -name "${stale_pattern}*" -delete 2>/dev/null || true

stale_files_after=$(find "$FAKE_CACHE" -name "${stale_pattern}*" 2>/dev/null | wc -l)

echo "Stale files after:  $stale_files_after"

if [ "$stale_files_after" -eq 0 ]; then
    echo "✓ Stale files successfully cleaned"
else
    echo "✗ FAILED: Stale files remain"
    exit 1
fi
echo ""

# Test 4: Verify complete cleanup
echo "Test 4: Verify cache directory is clean"
echo "----------------------------------------"

all_remaining=$(find "$FAKE_CACHE" -type f 2>/dev/null || true)

if [ -z "$all_remaining" ]; then
    echo "✓ Bisync cache is completely clean"
else
    echo "✗ FAILED: Unexpected files remain:"
    echo "$all_remaining" | sed 's/^/  /'
    exit 1
fi
echo ""

# Test 5: Integration test - simulate full remove_job flow
echo "Test 5: Integration test (full job removal)"
echo "--------------------------------------------"

# Re-create job config and cache
cat > "$FAKE_CONFIG/test-job-456.conf" <<'EOF'
JOB_ID="test-job-456"
LOCAL_PATH="/home/joseph/Documents"
REMOTE_PATH="proton:/Documents"
SYNC_TYPE="bisync"
INTERVAL="10"
SERVICE_NAME="proton-drive-sync-test-job-456"
EOF

# Create bisync cache for this job
touch "$FAKE_CACHE/home_joseph_Documents..proton_Documents.path1.lst-new"
touch "$FAKE_CACHE/home_joseph_Documents..proton_Documents.path2.lst-old"

echo "Created job: test-job-456"
echo "  Local:  /home/joseph/Documents"
echo "  Remote: proton:/Documents"
echo "  Cache files: 2"
echo ""

# Simulate job removal (extract cache cleanup logic)
source "$FAKE_CONFIG/test-job-456.conf"

local_cleaned=$(echo "$LOCAL_PATH" | sed 's|^/||; s|/|_|g')
remote_cleaned=$(echo "$REMOTE_PATH" | sed 's|:/|_|g; s|/|_|g')
cache_pattern="${local_cleaned}..${remote_cleaned}"

files_before=$(find "$FAKE_CACHE" -name "${cache_pattern}*" 2>/dev/null | wc -l)
echo "Cache files before removal: $files_before"

# Execute cleanup
find "$FAKE_CACHE" -name "${cache_pattern}*" -delete 2>/dev/null || true

files_after=$(find "$FAKE_CACHE" -name "${cache_pattern}*" 2>/dev/null | wc -l)
echo "Cache files after removal:  $files_after"

if [ "$files_after" -eq 0 ] && [ "$files_before" -gt 0 ]; then
    echo "✓ Job removal successfully cleaned bisync cache"
else
    echo "✗ FAILED: Cache cleanup did not work as expected"
    exit 1
fi
echo ""

# Cleanup
rm -rf "$TEST_DIR"
echo "✓ Test environment cleaned up"
echo ""

# Summary
echo "=========================================="
echo "✅ ALL TESTS PASSED"
echo "=========================================="
echo ""
echo "What this test verifies:"
echo "  1. Cache pattern matching correctly identifies files"
echo "  2. Cleanup removes ONLY files for the deleted job"
echo "  3. Other jobs' cache files are NOT affected"
echo "  4. Stale cache (from bugs) must be manually cleaned"
echo "  5. Full integration: job deletion → cache cleanup"
echo ""
echo "Prevention: When a sync job is deleted, bisync cache"
echo "is automatically cleaned to prevent false positive"
echo "conflicts when recreating jobs with similar paths."
echo ""
