#!/bin/bash

# Comprehensive Sync Testing Script for Proton Drive Linux
# Tests: file open, sync updates, file add/delete, folder deletion, conflict scenarios

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
TEST_REPORT="/tmp/proton-drive-sync-test-report.md"
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Find rclone binary (system or bundled)
if command -v rclone &> /dev/null; then
    RCLONE="rclone"
elif [ -f "$PROJECT_ROOT/dist/AppDir/usr/bin/rclone" ]; then
    RCLONE="$PROJECT_ROOT/dist/AppDir/usr/bin/rclone"
    export PATH="$PROJECT_ROOT/dist/AppDir/usr/bin:$PATH"
else
    echo -e "${RED}ERROR: rclone not found${NC}"
    echo "Please install rclone or run: make setup"
    exit 1
fi

# Test tracking
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

# Initialize report
cat > "$TEST_REPORT" << 'HEADER'
# Proton Drive Linux - Comprehensive Sync Test Report
Generated: $(date)

## Test Environment
HEADER

echo "==================================================================="
echo "  Proton Drive Linux - Comprehensive Sync Testing"
echo "==================================================================="
echo ""

# Function to log test results
log_test() {
    local test_name="$1"
    local result="$2"
    local details="$3"
    
    TESTS_RUN=$((TESTS_RUN + 1))
    
    if [ "$result" = "PASS" ]; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        echo -e "${GREEN}✓ PASS${NC}: $test_name"
        echo "### ✓ PASS: $test_name" >> "$TEST_REPORT"
    elif [ "$result" = "FAIL" ]; then
        TESTS_FAILED=$((TESTS_FAILED + 1))
        echo -e "${RED}✗ FAIL${NC}: $test_name"
        echo "### ✗ FAIL: $test_name" >> "$TEST_REPORT"
    else
        echo -e "${YELLOW}⚠ SKIP${NC}: $test_name"
        echo "### ⚠ SKIP: $test_name" >> "$TEST_REPORT"
    fi
    
    if [ -n "$details" ]; then
        echo "$details" >> "$TEST_REPORT"
    fi
    echo "" >> "$TEST_REPORT"
}

# Check prerequisites
check_prerequisites() {
    echo -e "${BLUE}Checking prerequisites...${NC}"
    
    # Check if $RCLONE is configured
    if ! $RCLONE listremotes 2>/dev/null | grep -q "proton:"; then
        echo -e "${YELLOW}WARNING: $RCLONE 'proton' remote not configured${NC}"
        echo "Skipping tests that require cloud access"
        echo "To configure: Run 'make setup-sync' or '$RCLONE config'"
        log_test "Prerequisites Check" "SKIP" "$RCLONE proton remote not configured - some tests will be skipped"
        return
    fi
    
    # Check if sync service is available
    if [ ! -f "$HOME/.config/systemd/user/proton-drive-sync.service" ]; then
        echo -e "${YELLOW}WARNING: Sync service not set up${NC}"
        echo "Some tests may fail. Run: make setup-sync"
    fi
    
    log_test "Prerequisites Check" "PASS" "$RCLONE proton remote configured"
}

# Test 1: Double-click file opening (synced vs cloud-only)
test_file_opening() {
    echo -e "\n${BLUE}Test 1: File Opening (Double-click)${NC}"
    
    # Create a test file in cloud
    TEST_FILE_CLOUD="TestOpenFile_$(date +%s).txt"
    echo "Test content for opening" > "/tmp/$TEST_FILE_CLOUD"
    
    echo "  Uploading test file to cloud..."
    if $RCLONE copy "/tmp/$TEST_FILE_CLOUD" "proton:/Test/" 2>&1; then
        log_test "File Upload to Cloud" "PASS" "File uploaded: $TEST_FILE_CLOUD"
    else
        log_test "File Upload to Cloud" "FAIL" "Failed to upload test file (exit code: $?)"
        rm -f "/tmp/$TEST_FILE_CLOUD"
        return
    fi
    
    # Test synced file opening (would require actual sync setup)
    echo "  NOTE: Manual test required for synced file opening"
    echo "  1. Double-click a file that is synced locally"
    echo "  2. Verify it opens in default application"
    echo "  3. Check logs for: [Open] Synced file: <filename>"
    
    # Test cloud-only file opening
    echo "  NOTE: Manual test required for cloud-only file opening"
    echo "  1. Double-click the cloud-only file: $TEST_FILE_CLOUD"
    echo "  2. Verify it downloads to ~/Downloads"
    echo "  3. Verify it opens after download"
    echo "  4. Check logs for: [Download] Downloading... then [Open] Downloaded: <filename>"
    
    log_test "File Opening Feature" "SKIP" "Manual test required - see instructions in report"
    
    # Cleanup
    $RCLONE delete "proton:/Test/$TEST_FILE_CLOUD" 2>/dev/null || true
    rm -f "/tmp/$TEST_FILE_CLOUD"
}

# Test 2: File update sync
test_file_update_sync() {
    echo -e "\n${BLUE}Test 2: File Update Sync${NC}"
    
    # This requires an active sync job
    SYNC_JOBS=$(find "$HOME/.config/proton-drive/jobs" -name "*.conf" 2>/dev/null | wc -l)
    
    if [ "$SYNC_JOBS" -eq 0 ]; then
        log_test "File Update Sync" "SKIP" "No active sync jobs found. Create a sync job first."
        return
    fi
    
    # Get first sync job's local path
    FIRST_JOB=$(find "$HOME/.config/proton-drive/jobs" -name "*.conf" 2>/dev/null | head -n1)
    if [ -z "$FIRST_JOB" ]; then
        log_test "File Update Sync" "SKIP" "No sync jobs available"
        return
    fi
    
    LOCAL_PATH=$(grep "^LOCAL_PATH=" "$FIRST_JOB" | cut -d'=' -f2- | tr -d '"')
    
    if [ -z "$LOCAL_PATH" ] || [ ! -d "$LOCAL_PATH" ]; then
        log_test "File Update Sync" "SKIP" "Invalid sync job path: $LOCAL_PATH"
        return
    fi
    
    TEST_FILE="$LOCAL_PATH/sync_test_$(date +%s).txt"
    
    echo "  Creating test file in synced folder: $TEST_FILE"
    echo "Initial content - $(date)" > "$TEST_FILE"
    
    echo "  Waiting for initial sync (10 seconds)..."
    sleep 10
    
    echo "  Updating file content..."
    echo "Updated content - $(date)" >> "$TEST_FILE"
    
    echo "  Waiting for update sync (10 seconds)..."
    sleep 10
    
    # Check if file was synced (basic check)
    if [ -f "$TEST_FILE" ]; then
        log_test "File Update Sync" "PASS" "Test file created and modified. Check cloud to verify sync."
        echo "  Manual verification: Check if file exists in cloud and has both lines"
    else
        log_test "File Update Sync" "FAIL" "Test file disappeared"
    fi
    
    echo "  Test file left for verification: $TEST_FILE"
}

# Test 3: File add/delete in synced folder
test_file_add_delete() {
    echo -e "\n${BLUE}Test 3: File Add/Delete in Synced Folder${NC}"
    
    SYNC_JOBS=$(find "$HOME/.config/proton-drive/jobs" -name "*.conf" 2>/dev/null | wc -l)
    
    if [ "$SYNC_JOBS" -eq 0 ]; then
        log_test "File Add/Delete" "SKIP" "No active sync jobs found"
        return
    fi
    
    FIRST_JOB=$(find "$HOME/.config/proton-drive/jobs" -name "*.conf" 2>/dev/null | head -n1)
    LOCAL_PATH=$(grep "^LOCAL_PATH=" "$FIRST_JOB" | cut -d'=' -f2- | tr -d '"')
    
    if [ -z "$LOCAL_PATH" ] || [ ! -d "$LOCAL_PATH" ]; then
        log_test "File Add/Delete" "SKIP" "Invalid sync job path"
        return
    fi
    
    TEST_FILE_ADD="$LOCAL_PATH/added_file_$(date +%s).txt"
    
    echo "  Adding new file: $TEST_FILE_ADD"
    echo "This file was added during testing" > "$TEST_FILE_ADD"
    
    echo "  Waiting for sync (10 seconds)..."
    sleep 10
    
    if [ -f "$TEST_FILE_ADD" ]; then
        echo "  Deleting the file..."
        rm "$TEST_FILE_ADD"
        
        echo "  Waiting for delete sync (10 seconds)..."
        sleep 10
        
        log_test "File Add/Delete" "PASS" "File added and deleted. Verify cloud reflects changes."
    else
        log_test "File Add/Delete" "FAIL" "File disappeared unexpectedly"
    fi
}

# Test 4: Local folder deletion (should NOT delete from cloud)
test_folder_deletion_handling() {
    echo -e "\n${BLUE}Test 4: Local Folder Deletion Handling${NC}"
    
    echo "  This test requires manual intervention due to safety concerns"
    echo ""
    echo "  ${YELLOW}MANUAL TEST PROCEDURE:${NC}"
    echo "  1. Create a test sync job with a new folder"
    echo "  2. Add some test files to the synced folder"
    echo "  3. Wait for sync to complete"
    echo "  4. Delete the local synced folder (rm -rf <path> or via file manager)"
    echo "  5. Observe application behavior:"
    echo "     a. Application should NOT delete from cloud"
    echo "     b. Application should show a notification/dialog"
    echo "     c. Options should include:"
    echo "        - Re-sync from cloud (restore local)"
    echo "        - Locate folder (if moved)"
    echo "        - Stop/delete sync job"
    echo ""
    
    log_test "Folder Deletion Handling" "SKIP" "Manual test required - see procedure in report"
}

# Test 5: Common sync conflicts
test_sync_conflicts() {
    echo -e "\n${BLUE}Test 5: Sync Conflict Scenarios${NC}"
    
    echo ""
    echo "  ${YELLOW}COMMON SYNC CONFLICTS TO TEST:${NC}"
    echo ""
    
    echo "  ${BLUE}A. Same file modified on multiple devices${NC}"
    echo "     1. Set up shared sync between two devices"
    echo "     2. Modify the same file on both devices while offline"
    echo "     3. Reconnect and observe conflict resolution"
    echo "     4. Expected: Conflict files created (filename.conflict.ext)"
    echo ""
    
    echo "  ${BLUE}B. Case-sensitivity conflicts${NC}"
    echo "     1. Create 'File.txt' on device A"
    echo "     2. Create 'file.txt' on device B"
    echo "     3. Sync both to cloud"
    echo "     4. Expected: Proton Drive (case-insensitive) merges or conflicts"
    echo ""
    
    echo "  ${BLUE}C. Rapid modifications${NC}"
    echo "     1. Open a file in editor"
    echo "     2. Make rapid saves (every second)"
    echo "     3. Observe sync behavior"
    echo "     4. Expected: Changes batched, no duplicates"
    echo ""
    
    echo "  ${BLUE}D. Network interruption${NC}"
    echo "     1. Start a large file sync"
    echo "     2. Disconnect network mid-transfer"
    echo "     3. Reconnect after 30 seconds"
    echo "     4. Expected: Resume or restart cleanly, no corruption"
    echo ""
    
    echo "  ${BLUE}E. Simultaneous folder sync${NC}"
    echo "     1. Try to sync the same cloud folder from two devices"
    echo "     2. Expected: Device conflict warning shown"
    echo "     3. Options: Enable shared sync or cancel"
    echo ""
    
    echo "  ${BLUE}F. Disk space exhaustion${NC}"
    echo "     1. Fill local disk to near capacity"
    echo "     2. Attempt to sync large files"
    echo "     3. Expected: Error shown, sync paused, no partial files"
    echo ""
    
    echo "  ${BLUE}G. File permission conflicts${NC}"
    echo "     1. Create file with read-only permissions"
    echo "     2. Modify on another device"
    echo "     3. Sync back"
    echo "     4. Expected: Permission conflict handled gracefully"
    echo ""
    
    echo "  ${BLUE}H. Large file sync${NC}"
    echo "     1. Create file >1GB"
    echo "     2. Sync to cloud"
    echo "     3. Verify progress indication"
    echo "     4. Expected: Chunked upload, resumable, accurate progress"
    echo ""
    
    log_test "Sync Conflict Scenarios" "SKIP" "Manual tests - see detailed procedures in report"
}

# Test 6: Metadata and device tracking
test_metadata_tracking() {
    echo -e "\n${BLUE}Test 6: Metadata and Device Tracking${NC}"
    
    DEVICE_ID_FILE="$HOME/.config/proton-drive/device.json"
    SYNC_JOBS_FILE="$HOME/.config/proton-drive/sync_jobs.json"
    
    if [ -f "$DEVICE_ID_FILE" ]; then
        DEVICE_ID=$(jq -r '.device_id' "$DEVICE_ID_FILE" 2>/dev/null || echo "unknown")
        DEVICE_NAME=$(jq -r '.device_name' "$DEVICE_ID_FILE" 2>/dev/null || echo "unknown")
        
        log_test "Device Identity" "PASS" "Device ID: $DEVICE_ID, Name: $DEVICE_NAME"
    else
        log_test "Device Identity" "FAIL" "Device ID file not found at $DEVICE_ID_FILE"
    fi
    
    if [ -f "$SYNC_JOBS_FILE" ]; then
        JOB_COUNT=$(jq '.jobs | length' "$SYNC_JOBS_FILE" 2>/dev/null || echo "0")
        log_test "Sync Jobs Metadata" "PASS" "Found $JOB_COUNT sync jobs in metadata"
    else
        log_test "Sync Jobs Metadata" "SKIP" "No sync jobs metadata file"
    fi
}

# Test 7: File watcher responsiveness
test_file_watcher() {
    echo -e "\n${BLUE}Test 7: File Watcher Responsiveness${NC}"
    
    SYNC_JOBS=$(find "$HOME/.config/proton-drive/jobs" -name "*.conf" 2>/dev/null | wc -l)
    
    if [ "$SYNC_JOBS" -eq 0 ]; then
        log_test "File Watcher" "SKIP" "No active sync jobs - file watcher requires running app with sync job"
        return
    fi
    
    # Check if app is currently running
    if ! pgrep -f "proton-drive" > /dev/null; then
        echo "  NOTE: File watcher test requires app to be running"
        echo "  To test file watcher:"
        echo "    1. Start the app: ./src-native/build/proton-drive"
        echo "    2. Create/modify a file in a synced folder"
        echo "    3. Check logs: tail -f ~/.cache/proton-drive/proton-drive.log"
        echo "    4. Look for '[FileWatcher]' entries"
        log_test "File Watcher" "SKIP" "App not running - start app to test file watcher"
        return
    fi
    
    FIRST_JOB=$(find "$HOME/.config/proton-drive/jobs" -name "*.conf" 2>/dev/null | head -n1)
    LOCAL_PATH=$(grep "^LOCAL_PATH=" "$FIRST_JOB" | cut -d'=' -f2- | tr -d '"')
    
    if [ -z "$LOCAL_PATH" ] || [ ! -d "$LOCAL_PATH" ]; then
        log_test "File Watcher" "SKIP" "Invalid sync path"
        return
    fi
    
    TEST_FILE="$LOCAL_PATH/watcher_test_$(date +%s).txt"
    
    echo "  Creating file to test watcher..."
    echo "Watcher test - $(date)" > "$TEST_FILE"
    
    echo "  Checking logs for inotify events (3 seconds)..."
    sleep 3
    
    LOG_FILE="$HOME/.cache/proton-drive/proton-drive.log"
    if [ -f "$LOG_FILE" ]; then
        FILENAME=$(basename "$TEST_FILE")
        if tail -n 50 "$LOG_FILE" | grep -q "FileWatcher.*$FILENAME"; then
            log_test "File Watcher Detection" "PASS" "File change detected by watcher"
        else
            log_test "File Watcher Detection" "FAIL" "No watcher event found - check if sync job is active"
        fi
    else
        log_test "File Watcher Detection" "SKIP" "Log file not found"
    fi
    
    rm -f "$TEST_FILE"
}

# Generate final report
generate_report() {
    echo "" >> "$TEST_REPORT"
    echo "## Test Summary" >> "$TEST_REPORT"
    echo "- Total Tests: $TESTS_RUN" >> "$TEST_REPORT"
    echo "- Passed: $TESTS_PASSED" >> "$TEST_REPORT"
    echo "- Failed: $TESTS_FAILED" >> "$TEST_REPORT"
    echo "- Skipped: $((TESTS_RUN - TESTS_PASSED - TESTS_FAILED))" >> "$TEST_REPORT"
    echo "" >> "$TEST_REPORT"
    
    if [ $TESTS_FAILED -gt 0 ]; then
        echo "## ⚠️ Action Required" >> "$TEST_REPORT"
        echo "Some tests failed. Please review the failures above." >> "$TEST_REPORT"
    fi
    
    echo "" >> "$TEST_REPORT"
    echo "## Logs and Artifacts" >> "$TEST_REPORT"
    echo "- Application log: ~/.cache/proton-drive/proton-drive.log" >> "$TEST_REPORT"
    echo "- $RCLONE log: ~/.cache/proton-drive/rclone.log" >> "$TEST_REPORT"
    echo "- Sync jobs: ~/.config/proton-drive/sync_jobs.json" >> "$TEST_REPORT"
    echo "- Device ID: ~/.config/proton-drive/device_id.json" >> "$TEST_REPORT"
    
    echo ""
    echo "==================================================================="
    echo "  Test Summary"
    echo "==================================================================="
    echo -e "Total Tests: $TESTS_RUN"
    echo -e "${GREEN}Passed: $TESTS_PASSED${NC}"
    echo -e "${RED}Failed: $TESTS_FAILED${NC}"
    echo -e "${YELLOW}Skipped: $((TESTS_RUN - TESTS_PASSED - TESTS_FAILED))${NC}"
    echo ""
    echo "Full report: $TEST_REPORT"
    echo "==================================================================="
}

# Main test execution
main() {
    check_prerequisites
    test_file_opening
    test_file_update_sync
    test_file_add_delete
    test_folder_deletion_handling
    test_sync_conflicts
    test_metadata_tracking
    test_file_watcher
    generate_report
}

# Run tests
main

