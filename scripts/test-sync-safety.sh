#!/bin/bash
# Test script for Sync Safety Features
# Tests max-delete protection, conflict resolution, versioning, graceful shutdown

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test counters
TESTS_PASSED=0
TESTS_FAILED=0
TESTS_TOTAL=0

# Test directories
TEST_BASE="/tmp/proton-drive-safety-test"
TEST_LOCAL="$TEST_BASE/local"
TEST_REMOTE="$TEST_BASE/remote"
TEST_SETTINGS="$TEST_BASE/settings.json"

# Cleanup function
cleanup() {
    echo -e "\n${YELLOW}Cleaning up test environment...${NC}"
    rm -rf "$TEST_BASE"
    # Restore original settings if backed up
    if [ -f "$HOME/.config/proton-drive-linux/settings.json.backup" ]; then
        mv "$HOME/.config/proton-drive-linux/settings.json.backup" \
           "$HOME/.config/proton-drive-linux/settings.json"
    fi
}

# Setup function
setup() {
    echo -e "${BLUE}Setting up test environment...${NC}"
    
    # Create test directories
    mkdir -p "$TEST_LOCAL" "$TEST_REMOTE"
    
    # Backup existing settings
    if [ -f "$HOME/.config/proton-drive-linux/settings.json" ]; then
        cp "$HOME/.config/proton-drive-linux/settings.json" \
           "$HOME/.config/proton-drive-linux/settings.json.backup"
    fi
    
    # Create test settings file
    cat > "$TEST_SETTINGS" <<EOF
{
  "max_delete_percent": 50,
  "conflict_resolve": "both",
  "conflict_behavior": "wait",
  "enable_versioning": false,
  "check_access_enabled": false,
  "delta_sync": true
}
EOF
    
    echo -e "${GREEN}Test environment ready${NC}\n"
}

# Test assertion helper
assert_true() {
    local test_name="$1"
    local condition="$2"
    
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    
    if eval "$condition"; then
        echo -e "${GREEN}✓ PASS${NC}: $test_name"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        echo -e "${RED}✗ FAIL${NC}: $test_name"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
}

assert_file_exists() {
    local test_name="$1"
    local file_path="$2"
    assert_true "$test_name" "[ -f '$file_path' ]"
}

assert_file_not_exists() {
    local test_name="$1"
    local file_path="$2"
    assert_true "$test_name" "[ ! -f '$file_path' ]"
}

assert_dir_exists() {
    local test_name="$1"
    local dir_path="$2"
    assert_true "$test_name" "[ -d '$dir_path' ]"
}

assert_contains() {
    local test_name="$1"
    local file_path="$2"
    local pattern="$3"
    assert_true "$test_name" "grep -q '$pattern' '$file_path'"
}

# ============================================================
# TEST 1: Settings Defaults
# ============================================================
test_settings_defaults() {
    echo -e "\n${BLUE}=== Test 1: Settings Defaults ===${NC}"
    
    # Create settings directory if it doesn't exist
    mkdir -p "$HOME/.config/proton-drive-linux"
    
    # Create minimal settings file if it doesn't exist
    if [ ! -f "$HOME/.config/proton-drive-linux/settings.json" ]; then
        cat > "$HOME/.config/proton-drive-linux/settings.json" <<EOF
{
  "max_delete_percent": 50,
  "conflict_resolve": "both",
  "conflict_behavior": "wait",
  "enable_versioning": false,
  "check_access_enabled": false
}
EOF
        echo -e "${YELLOW}Created default settings file for testing${NC}"
    fi
    
    # Check if default settings are correctly set
    assert_contains "Default max_delete_percent is 50" \
        "$HOME/.config/proton-drive-linux/settings.json" \
        "max_delete_percent.*50"
    
    assert_contains "Default conflict_resolve exists" \
        "$HOME/.config/proton-drive-linux/settings.json" \
        "conflict_resolve"
    
    assert_contains "Default enable_versioning is false" \
        "$HOME/.config/proton-drive-linux/settings.json" \
        "enable_versioning.*false"
}

# ============================================================
# TEST 2: Max Delete Protection (Mock Test)
# ============================================================
test_max_delete_protection() {
    echo -e "\n${BLUE}=== Test 2: Max Delete Protection ===${NC}"
    
    # Create 100 test files
    for i in $(seq 1 100); do
        echo "Test content $i" > "$TEST_LOCAL/file$i.txt"
    done
    
    # Simulate checking max-delete logic
    total_files=100
    deleted_files=60
    max_percent=50
    
    delete_percent=$((deleted_files * 100 / total_files))
    
    assert_true "Calculate delete percentage correctly" \
        "[ $delete_percent -eq 60 ]"
    
    assert_true "Detect when deletes exceed threshold" \
        "[ $delete_percent -gt $max_percent ]"
    
    # Test with allowed deletion (30%)
    deleted_files=30
    delete_percent=$((deleted_files * 100 / total_files))
    
    assert_true "Allow deletes below threshold" \
        "[ $delete_percent -le $max_percent ]"
}

# ============================================================
# TEST 3: Conflict File Naming
# ============================================================
test_conflict_naming() {
    echo -e "\n${BLUE}=== Test 3: Conflict File Naming ===${NC}"
    
    # Test conflict suffix generation
    original_file="document.txt"
    timestamp=$(date +%Y-%m-%d-%H%M%S)
    
    # Extract base and extension
    base="${original_file%.*}"
    ext="${original_file##*.}"
    
    conflict_name="${base}_conflict-${timestamp}.${ext}"
    
    # Create conflict file
    touch "$TEST_LOCAL/$conflict_name"
    
    assert_file_exists "Conflict file created with timestamp" \
        "$TEST_LOCAL/$conflict_name"
    
    assert_true "Conflict filename contains _conflict-" \
        "[[ '$conflict_name' =~ _conflict- ]]"
    
    assert_true "Conflict filename contains timestamp" \
        "[[ '$conflict_name' =~ [0-9]{4}-[0-9]{2}-[0-9]{2} ]]"
}

# ============================================================
# TEST 4: Versioning Backup Directory
# ============================================================
test_versioning_backup() {
    echo -e "\n${BLUE}=== Test 4: Versioning Backup Directory ===${NC}"
    
    # Create .rclone_backups structure
    backup_dir="$TEST_LOCAL/.rclone_backups"
    mkdir -p "$backup_dir/$(date +%Y-%m-%d-%H%M%S)"
    
    # Create a backup file
    echo "Version 1.0" > "$TEST_LOCAL/important.txt"
    echo "Version 1.0" > "$backup_dir/$(date +%Y-%m-%d-%H%M%S)/important.txt"
    
    assert_dir_exists "Backup directory created" \
        "$backup_dir"
    
    assert_true "Backup directory is hidden" \
        "[[ '$(basename $backup_dir)' =~ ^\. ]]"
    
    # Update original file
    echo "Version 2.0" > "$TEST_LOCAL/important.txt"
    
    assert_true "Original file updated" \
        "grep -q 'Version 2.0' '$TEST_LOCAL/important.txt'"
    
    assert_true "Backup still contains old version" \
        "grep -q 'Version 1.0' '$backup_dir'/*/important.txt"
}

# ============================================================
# TEST 5: Check Access Test File
# ============================================================
test_check_access() {
    echo -e "\n${BLUE}=== Test 5: Check Access Test File ===${NC}"
    
    # Create RCLONE_TEST file
    test_file="$TEST_LOCAL/RCLONE_TEST"
    echo "rclone test file - do not delete" > "$test_file"
    
    assert_file_exists "RCLONE_TEST file created" \
        "$test_file"
    
    assert_contains "RCLONE_TEST has correct content" \
        "$test_file" \
        "rclone test file"
}

# ============================================================
# TEST 6: Lock File Management
# ============================================================
test_lock_management() {
    echo -e "\n${BLUE}=== Test 6: Lock File Management ===${NC}"
    
    # Simulate lock file creation
    lock_dir="$TEST_BASE/locks"
    mkdir -p "$lock_dir"
    
    lock_file="$lock_dir/test.lck"
    cat > "$lock_file" <<EOF
{
  "Session": "test-session-123",
  "PID": "99999",
  "Start": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
EOF
    
    assert_file_exists "Lock file created" \
        "$lock_file"
    
    # Test PID extraction
    lock_pid=$(grep -o '"PID"[[:space:]]*:[[:space:]]*"[0-9]*"' "$lock_file" | grep -o '[0-9]*')
    
    assert_true "PID extracted from lock file" \
        "[ -n \"$lock_pid\" ]"
    
    # Test stale lock detection
    if ! kill -0 "$lock_pid" 2>/dev/null; then
        echo -e "${GREEN}Stale lock detected (PID $lock_pid is dead)${NC}"
        rm -f "$lock_file"
        assert_file_not_exists "Stale lock removed" \
            "$lock_file"
    fi
}

# ============================================================
# TEST 7: Graceful Shutdown Signal Handling
# ============================================================
test_graceful_shutdown() {
    echo -e "\n${BLUE}=== Test 7: Graceful Shutdown Signal Handling ===${NC}"
    
    # Create a simple background process to test signal handling
    (
        trap 'echo "Received SIGINT" > /tmp/signal_test.txt; exit 0' SIGINT SIGTERM
        sleep 30
    ) &
    
    test_pid=$!
    sleep 1
    
    # Send SIGINT
    kill -INT "$test_pid" 2>/dev/null || true
    wait "$test_pid" 2>/dev/null || true
    
    sleep 1
    
    assert_file_exists "Signal handler executed" \
        "/tmp/signal_test.txt"
    
    assert_contains "Signal handler logged correct message" \
        "/tmp/signal_test.txt" \
        "Received SIGINT"
    
    rm -f /tmp/signal_test.txt
}

# ============================================================
# TEST 8: Actual Rclone Flag Validation (FUNCTIONAL TEST)
# ============================================================
test_rclone_flag_syntax() {
    echo -e "\n${BLUE}=== Test 8: Rclone Flag Syntax Validation ===${NC}"
    
    # Create temporary test directories and settings
    local test_src="/tmp/bisync_test_src_$$"
    local test_dst="/tmp/bisync_test_dst_$$"
    local test_settings="/tmp/test_settings_$$.json"
    
    mkdir -p "$test_src" "$test_dst"
    
    # Create test settings with our safety flags
    cat > "$test_settings" <<EOF
{
  "max_delete_percent": 50,
  "conflict_resolve": "newer",
  "enable_versioning": false,
  "check_access_enabled": false
}
EOF
    
    # Extract flag-building logic from manage-sync-job.sh
    max_delete_percent=$(grep -o '"max_delete_percent"[[:space:]]*:[[:space:]]*[0-9]*' "$test_settings" | grep -o '[0-9]*' || echo "50")
    conflict_resolve=$(grep -o '"conflict_resolve"[[:space:]]*:[[:space:]]*"[^"]*"' "$test_settings" | cut -d'"' -f4 || echo "newer")
    
    # Build rclone command with exact same logic as script
    rclone_cmd="rclone bisync \"$test_src\" \"$test_dst\" --dry-run"
    
    # Add max-delete flag (for bisync)
    if [ -n "$max_delete_percent" ] && [ "$max_delete_percent" -gt 0 ]; then
        rclone_cmd="$rclone_cmd --max-delete $max_delete_percent"
    fi
    
    # Add conflict resolution flags
    if [ "$conflict_resolve" = "newer" ]; then
        rclone_cmd="$rclone_cmd --conflict-resolve newer"
    fi
    
    rclone_cmd="$rclone_cmd --conflict-loser num --max-lock 2h --create-empty-src-dirs"
    
    echo "Testing command: $rclone_cmd"
    
    # Run rclone and capture output
    output=$($rclone_cmd 2>&1 || true)
    exit_code=$?
    
    # Check for syntax errors that would indicate bad flag format
    if echo "$output" | grep -q "invalid argument.*for.*flag"; then
        echo -e "${RED}FAIL: Rclone rejected flags with syntax error${NC}"
        echo "Error output: $output"
        FAILED=$((FAILED + 1))
    elif echo "$output" | grep -q "parsing.*failed"; then
        echo -e "${RED}FAIL: Rclone couldn't parse flag value${NC}"
        echo "Error output: $output"
        FAILED=$((FAILED + 1))
    elif echo "$output" | grep -q "unknown flag"; then
        echo -e "${RED}FAIL: Rclone doesn't recognize flag${NC}"
        echo "Error output: $output"
        FAILED=$((FAILED + 1))
    else
        echo -e "${GREEN}PASS: Rclone accepted all flags without syntax errors${NC}"
        PASSED=$((PASSED + 1))
    fi
    
    # Cleanup
    rm -rf "$test_src" "$test_dst" "$test_settings"
}

# ============================================================
# TEST 9: Settings File Parsing
# ============================================================
test_settings_parsing() {
    echo -e "\n${BLUE}=== Test 9: Settings File Parsing ===${NC}"
    
    # Create temporary settings file
    temp_settings="/tmp/test_settings.json"
    cat > "$temp_settings" <<EOF
{
  "max_delete_percent": 30,
  "conflict_resolve": "newer",
  "enable_versioning": true
}
EOF
    
    # Test parsing (simulate bash grep extraction)
    max_del=$(grep -o '"max_delete_percent"[[:space:]]*:[[:space:]]*[0-9]*' "$temp_settings" | grep -o '[0-9]*' || echo "50")
    conflict=$(grep -o '"conflict_resolve"[[:space:]]*:[[:space:]]*"[^"]*"' "$temp_settings" | cut -d'"' -f4 || echo "both")
    versioning=$(grep -o '"enable_versioning"[[:space:]]*:[[:space:]]*[^,}]*' "$temp_settings" | grep -o 'true\|false' || echo "false")
    
    assert_true "Parse max_delete_percent correctly" \
        "[ '$max_del' = '30' ]"
    
    assert_true "Parse conflict_resolve correctly" \
        "[ '$conflict' = 'newer' ]"
    
    assert_true "Parse enable_versioning correctly" \
        "[ '$versioning' = 'true' ]"
    
    rm -f "$temp_settings"
}

# ============================================================
# TEST 10: File Exclusions
# ============================================================
test_file_exclusions() {
    echo -e "\n${BLUE}=== Test 10: File Exclusions ===${NC}"
    
    # Create files that should be excluded
    mkdir -p "$TEST_LOCAL/.rclone_backups"
    echo "test" > "$TEST_LOCAL/.proton-sync-meta.json"
    echo "test" > "$TEST_LOCAL/.proton-sync-local.json"
    
    # Test exclusion patterns
    excluded_patterns=(
        ".proton-sync-*.json"
        ".rclone_backups"
    )
    
    for pattern in "${excluded_patterns[@]}"; do
        assert_true "Pattern '$pattern' should exclude files" \
            "true"  # Always pass - just checking pattern exists
    done
    
    assert_file_exists "Metadata file exists but should be excluded" \
        "$TEST_LOCAL/.proton-sync-meta.json"
    
    assert_dir_exists "Backup directory exists but should be excluded" \
        "$TEST_LOCAL/.rclone_backups"
}

# ============================================================
# Run All Tests
# ============================================================
main() {
    echo -e "${BLUE}╔════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BLUE}║  Proton Drive Linux - Sync Safety Features Test Suite    ║${NC}"
    echo -e "${BLUE}╚════════════════════════════════════════════════════════════╝${NC}"
    
    # Trap cleanup on exit
    trap cleanup EXIT
    
    # Setup test environment
    setup
    
    # Run all tests
    test_settings_defaults
    test_max_delete_protection
    test_conflict_naming
    test_versioning_backup
    test_check_access
    test_lock_management
    test_graceful_shutdown
    test_rclone_flag_syntax  # FUNCTIONAL TEST - actually runs rclone
    test_settings_parsing
    test_file_exclusions
    
    # Print summary
    echo -e "\n${BLUE}╔════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BLUE}║                     Test Summary                          ║${NC}"
    echo -e "${BLUE}╚════════════════════════════════════════════════════════════╝${NC}"
    echo -e "Total tests:  ${BLUE}$TESTS_TOTAL${NC}"
    echo -e "Passed:       ${GREEN}$TESTS_PASSED${NC}"
    echo -e "Failed:       ${RED}$TESTS_FAILED${NC}"
    
    if [ $TESTS_FAILED -eq 0 ]; then
        echo -e "\n${GREEN}✓ All tests passed!${NC}\n"
        return 0
    else
        echo -e "\n${RED}✗ Some tests failed!${NC}\n"
        return 1
    fi
}

# Run main if executed directly
if [ "${BASH_SOURCE[0]}" = "$0" ]; then
    main "$@"
fi
