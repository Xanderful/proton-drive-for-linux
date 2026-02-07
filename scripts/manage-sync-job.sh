#!/bin/bash
# Proton Drive Linux - Sync Job Manager
# Manages systemd timers for periodic rclone sync/copy jobs

set -e

JOB_DIR="$HOME/.config/proton-drive/jobs"
SYSTEMD_DIR="$HOME/.config/systemd/user"
mkdir -p "$JOB_DIR"
mkdir -p "$SYSTEMD_DIR"

LOG_FILE="$HOME/.config/proton-drive/sync-manager.log"

# ============================================================
# GRACEFUL SHUTDOWN HANDLING
# ============================================================
# Track the rclone subprocess PID for clean shutdown
RCLONE_PID=""

# Graceful shutdown handler - forwards SIGINT to rclone for clean exit
graceful_shutdown() {
    echo "[Graceful Shutdown] Received termination signal..."
    if [ -n "$RCLONE_PID" ] && kill -0 "$RCLONE_PID" 2>/dev/null; then
        echo "[Graceful Shutdown] Sending SIGINT to rclone (PID: $RCLONE_PID)..."
        kill -INT "$RCLONE_PID" 2>/dev/null || true
        
        # Wait up to 30 seconds for rclone to finish gracefully
        for i in {1..30}; do
            if ! kill -0 "$RCLONE_PID" 2>/dev/null; then
                echo "[Graceful Shutdown] rclone exited cleanly."
                break
            fi
            sleep 1
        done
        
        # Force kill if still running
        if kill -0 "$RCLONE_PID" 2>/dev/null; then
            echo "[Graceful Shutdown] Force killing rclone..."
            kill -9 "$RCLONE_PID" 2>/dev/null || true
        fi
    fi
    
    # Kill any adaptive monitor we started
    if [ -n "$monitor_pid" ] && kill -0 "$monitor_pid" 2>/dev/null; then
        kill "$monitor_pid" 2>/dev/null || true
    fi
    
    echo "[Graceful Shutdown] Cleanup complete."
    exit 0
}

# Register the graceful shutdown handler
trap graceful_shutdown SIGINT SIGTERM SIGHUP
# ============================================================

# Auto-detect fixed rclone (v1.73.0-DEV with Proton Drive upload fix)
# See: https://github.com/rclone/rclone/pull/9081
if [ -x "/usr/local/bin/rclone-fixed" ]; then
    RCLONE_CMD="/usr/local/bin/rclone-fixed"
    echo "Using fixed rclone: $RCLONE_CMD"
elif command -v rclone-fixed &>/dev/null; then
    RCLONE_CMD="rclone-fixed"
    echo "Using fixed rclone: $RCLONE_CMD"
else
    RCLONE_CMD="rclone"
    # Check if standard rclone version has the fix (v1.73.0+)
    RCLONE_VER=$(rclone version 2>/dev/null | head -1 | grep -oP 'v\K[0-9]+\.[0-9]+' || echo "0.0")
    if awk -v ver="$RCLONE_VER" 'BEGIN { exit !(ver >= 1.73) }'; then
        echo "Using rclone $RCLONE_VER (upload fix included)"
    else
        echo "WARNING: rclone $RCLONE_VER may have Proton Drive upload issues."
        echo "Consider installing fixed rclone from: https://github.com/coderFrankenstain/rclone"
        echo "See issue: https://github.com/rclone/rclone/issues/8870"
    fi
fi

# Save original stdout to FD 3
exec 3>&1
# Redirect stdout/stderr to log
exec >> "$LOG_FILE" 2>&1

echo "--- $(date) :: Script called with: $* ---"

# List jobs
list_jobs() {
    # JSON output for the app to parse -> Send to FD 3
    echo "[" >&3
    first=true
    for file in "$JOB_DIR"/*.conf; do
        if [ -f "$file" ]; then
            if [ "$first" = true ]; then first=false; else echo "," >&3; fi
            source "$file"
            echo "  {" >&3
            echo "    \"id\": \"$JOB_ID\"," >&3
            echo "    \"local\": \"$LOCAL_PATH\"," >&3
            echo "    \"remote\": \"$REMOTE_PATH\"," >&3
            echo "    \"type\": \"$SYNC_TYPE\"," >&3
            echo "    \"interval\": \"$INTERVAL\"" >&3
            echo -n "  }" >&3
        fi
    done
    echo "" >&3
    echo "]" >&3
}

# Common function to create/update job service
create_or_update_job() {
    job_id="$1"
    local_path="$2"
    remote_path="$3"
    sync_type="$4"
    interval="$5"
    
    if [ -z "$local_path" ] || [ -z "$remote_path" ]; then
        echo "Error: Missing paths"
        return 1
    fi
    
    service_name="proton-drive-job-$job_id"
    
    # Save config
    echo "JOB_ID=\"$job_id\"" > "$JOB_DIR/$job_id.conf"
    echo "LOCAL_PATH=\"$local_path\"" >> "$JOB_DIR/$job_id.conf"
    echo "REMOTE_PATH=\"$remote_path\"" >> "$JOB_DIR/$job_id.conf"
    echo "SYNC_TYPE=\"$sync_type\"" >> "$JOB_DIR/$job_id.conf"
    echo "INTERVAL=\"$interval\"" >> "$JOB_DIR/$job_id.conf"
    echo "SERVICE_NAME=\"$service_name\"" >> "$JOB_DIR/$job_id.conf"
    
    # Create Systemd Service
    # Note: For bisync, normal runs should NOT have --resync
    # We will run the initial --resync manually below
    
    # Use standard rclone command for initial sync calls, but use "run" for the service
    # This allows the service to respond to system resources at runtime (Dynamic Allocation)
    SCRIPT_ABC_PATH=$(readlink -f "$0")
    
    cat > "$SYSTEMD_DIR/$service_name.service" <<EOF
[Unit]
Description=Proton Drive Sync Job $job_id
After=network.target

[Service]
Type=oneshot
ExecStart=$SCRIPT_ABC_PATH run "$local_path" "$remote_path" "$sync_type"
EOF


    # Create Systemd Timer
    cat > "$SYSTEMD_DIR/$service_name.timer" <<EOF
[Unit]
Description=Timer for Proton Drive Job $job_id

[Timer]
OnBootSec=5m
OnUnitActiveSec=$interval
Unit=$service_name.service

[Install]
WantedBy=timers.target
EOF

    
    # 3. Initial Run Logic
    echo "Performing initial sync..."
    
    # Ensure remote path exists (fixes bisync error if folder is missing)
    echo "Ensuring remote directory exists: $remote_path"
    $RCLONE_CMD mkdir "$remote_path" || true

    # Use run_dynamic_job for the initial run to get Adaptive Monitoring immediately
    # We background it to avoid blocking the UI, but it will log to the same place.
    
    if [ "$sync_type" = "bisync" ]; then
        echo "Initializing Two-Way Sync (Dynamic Mode)..."
        # Pass true for resync on first run
        (run_dynamic_job "$local_path" "$remote_path" "$sync_type" "true" || true) &
    else
        echo "Initializing One-Way Sync (Dynamic Mode)..."
        (run_dynamic_job "$local_path" "$remote_path" "$sync_type" || true) &
    fi

    # Enable and Start Timer
    systemctl --user daemon-reload
    systemctl --user enable --now "$service_name.timer"
    
}

# Add job (generates its own ID)
add_job() {
    # Generate ID
    job_id=$(date +%s%N | sha256sum | head -c 8)
    create_or_update_job "$job_id" "$1" "$2" "$3" "$4"
    echo "Job $job_id created and started."
}

# Create job with explicit ID (for C++ integration - ensures registry and .conf use same ID)
create_with_id() {
    job_id="$1"
    local_path="$2"
    remote_path="$3"
    sync_type="$4"
    interval="$5"
    
    if [ -z "$job_id" ] || [ -z "$local_path" ] || [ -z "$remote_path" ]; then
        echo "Error: Missing required arguments"
        echo "Usage: create-with-id <job_id> <local_path> <remote_path> <sync_type> <interval>"
        return 1
    fi
    
    create_or_update_job "$job_id" "$local_path" "$remote_path" "$sync_type" "$interval"
    echo "Job $job_id created with specified ID and started."
}

# Edit job
edit_job() {
    job_id="$1"
    local_path="$2"
    remote_path="$3"
    sync_type="$4"
    interval="$5"
    
    if [ ! -f "$JOB_DIR/$job_id.conf" ]; then
        echo "Job $job_id not found."
        exit 1
    fi
    
    # Update config and service
    create_or_update_job "$job_id" "$local_path" "$remote_path" "$sync_type" "$interval"
    echo "Job $job_id updated."
}

# Remove job
remove_job() {
    job_id="$1"
    if [ -f "$JOB_DIR/$job_id.conf" ]; then
        source "$JOB_DIR/$job_id.conf"
        # Stop both timer and the actual service if running
        systemctl --user stop "$SERVICE_NAME.timer"
        systemctl --user stop "$SERVICE_NAME.service"
        # Disable and remove
        systemctl --user disable "$SERVICE_NAME.timer"
        rm "$SYSTEMD_DIR/$SERVICE_NAME.service"
        rm "$SYSTEMD_DIR/$SERVICE_NAME.timer"
        systemctl --user daemon-reload
        rm "$JOB_DIR/$job_id.conf"
        
        # Kill any zombie processes for this job just in case
        # We search for the specific sync path to avoid killing other jobs
        if [ -n "$local_path" ]; then
            pkill -f "rclone.*$local_path" || true
        fi
        
        # CRITICAL: Clean up bisync cache for this job to prevent false positives
        # when recreating sync jobs with similar paths. The cache filename is
        # derived from LOCAL_PATH and REMOTE_PATH with special character handling.
        if [ -n "$LOCAL_PATH" ] && [ -n "$REMOTE_PATH" ]; then
            # Convert paths to bisync cache filename format:
            # Example: /home/user/Downloads + proton:/Downloads 
            #       -> home_user_Downloads..proton_Downloads.*
            # Note: rclone replaces "proton:/" with "proton_" (colon+slash becomes underscore)
            local local_cleaned=$(echo "$LOCAL_PATH" | sed 's|^/||; s|/|_|g')
            local remote_cleaned=$(echo "$REMOTE_PATH" | sed 's|:/|_|g; s|/|_|g')
            local cache_pattern="${local_cleaned}..${remote_cleaned}"
            
            local cache_dir="$HOME/.cache/rclone/bisync"
            if [ -d "$cache_dir" ]; then
                # Remove all cache files matching this job's pattern
                find "$cache_dir" -name "${cache_pattern}*" -delete 2>/dev/null || true
                echo "Cleaned bisync cache for job $job_id (pattern: ${cache_pattern}*)."
            fi
        fi
        
        # CRITICAL: Also remove from sync_jobs.json registry to keep C++ and shell in sync
        local registry_file="$HOME/.config/proton-drive/sync_jobs.json"
        if [ -f "$registry_file" ]; then
            # Create backup
            cp "$registry_file" "$registry_file.backup" 2>/dev/null || true
            
            # Remove this job from the registry using jq if available, otherwise use sed
            if command -v jq &>/dev/null; then
                jq --arg jid "$job_id" '.jobs |= map(select(.job_id != $jid))' \
                    "$registry_file" > "$registry_file.tmp" && \
                    mv "$registry_file.tmp" "$registry_file"
                echo "Removed job $job_id from sync_jobs.json registry."
            else
                # Fallback: Remove the job entry using sed (less robust but works)
                # Find the job block and delete it (simplified - assumes one job per block)
                sed -i.bak "/${job_id}/,/^  },/d; /^  }$/{ N; s/},\$/}/; }" "$registry_file" 2>/dev/null || true
                echo "Removed job $job_id from sync_jobs.json registry (using sed fallback)."
            fi
        fi
        
        echo "Job $job_id removed."
    else
        echo "Job not found."
        exit 1
    fi
}

# Trigger sync (standard)
trigger_job() {
    job_id="$1"
    if [ -f "$JOB_DIR/$job_id.conf" ]; then
        source "$JOB_DIR/$job_id.conf"
        systemctl --user start "$SERVICE_NAME.service"
        echo "Job $job_id started."
    else
        echo "Job not found."
        exit 1
    fi
}

# Force Resync (Repair bisync)
force_resync_job() {
    job_id="$1"
    if [ -f "$JOB_DIR/$job_id.conf" ]; then
        source "$JOB_DIR/$job_id.conf"
        
        systemctl --user stop "$SERVICE_NAME.timer"
        
        echo "Forcing resync for job $job_id..."
        if [ "$SYNC_TYPE" = "bisync" ]; then
            $RCLONE_CMD bisync "$LOCAL_PATH" "$REMOTE_PATH" --resync --verbose || true
        else
            $RCLONE_CMD sync "$LOCAL_PATH" "$REMOTE_PATH" --verbose || true
        fi
        
        systemctl --user start "$SERVICE_NAME.timer"
    else
        echo "Job not found."
        exit 1
    fi
}

# Dynamic Run Logic
run_dynamic_job() {
    local_path="$1"
    remote_path="$2"
    sync_type="$3"
    resync="$4"  # Optional: "true" or "--resync" to force resync
    
    # ============================================================
    # LOAD JOB EXCLUDES
    # ============================================================
    # Find the job config file that matches these paths and load excludes
    job_excludes=""
    for conf_file in "$JOB_DIR"/*.conf; do
        if [ -f "$conf_file" ]; then
            source "$conf_file"
            if [ "$LOCAL_PATH" = "$local_path" ] && [ "$REMOTE_PATH" = "$remote_path" ]; then
                if [ -n "$EXCLUDES" ]; then
                    echo "[Selective Sync] Found job excludes: $EXCLUDES"
                    # Convert comma-separated list to --exclude flags
                    IFS=',' read -ra EXCLUDE_ARRAY <<< "$EXCLUDES"
                    for excl in "${EXCLUDE_ARRAY[@]}"; do
                        if [ -n "$excl" ]; then
                            job_excludes="$job_excludes --exclude \"$excl/**\" --exclude \"$excl\""
                        fi
                    done
                fi
                break
            fi
        fi
    done
    # ============================================================
    
    # --- AUTO-RESYNC DETECTION FOR BISYNC ---
    # Check if bisync listing files exist for THIS SPECIFIC JOB; if not, we need --resync
    if [ "$sync_type" = "bisync" ] && [ "$resync" != "true" ] && [ "$resync" != "--resync" ]; then
        CACHE_DIR="$HOME/.cache/rclone/bisync"
        
        # rclone bisync naming convention uses path components in filenames
        # Format: {sanitized_path1}..{sanitized_path2}.path1.lst
        # We need to check for files that match BOTH our local and remote paths
        
        # Sanitize paths to match rclone's naming (replace / with _ and : with _)
        clean_local=$(basename "$local_path" | sed 's|[/:]|_|g')
        clean_remote=$(basename "$remote_path" | sed 's|[/:]|_|g')
        
        found_our_listing=false
        if [ -d "$CACHE_DIR" ]; then
            # Check for listing files that contain BOTH path components
            # This ensures we find the correct job's listing, not another job's
            for lst in "$CACHE_DIR"/*.path1.lst; do
                if [ -f "$lst" ]; then
                    lst_name=$(basename "$lst")
                    # Check if this listing file matches our paths
                    # rclone uses the full sanitized paths, so check for key identifiers
                    if echo "$lst_name" | grep -qi "$clean_local" && echo "$lst_name" | grep -qi "$clean_remote"; then
                        found_our_listing=true
                        echo "[Auto-Resync] Found matching listing: $lst_name"
                        
                        # Additional check: is the listing file empty or nearly empty?
                        # This catches stale listings from failed syncs
                        lst_size=$(stat -c%s "$lst" 2>/dev/null || echo "0")
                        if [ "$lst_size" -lt 50 ]; then
                            echo "[Auto-Resync] Listing file is too small ($lst_size bytes), likely stale."
                            found_our_listing=false
                        fi
                        break
                    fi
                fi
            done
            
            # If no matching listing found, also check for empty cache directory
            if [ "$found_our_listing" = false ]; then
                listing_count=$(find "$CACHE_DIR" -name "*.path1.lst" 2>/dev/null | wc -l)
                echo "[Auto-Resync] Found $listing_count total listing files, but none match this job."
            fi
        fi
        
        if [ "$found_our_listing" = false ]; then
            echo "[Auto-Resync] No valid bisync listings found for this job ($clean_local <-> $clean_remote). Running initial --resync..."
            resync="true"
        fi
    fi
    # -----------------------------------------
    
    # --- AUTO-HEALING: Stale Lock Removal ---
    if [ "$sync_type" = "bisync" ]; then
        # Check for stale lock files in standard rclone cache location
        # Path generation must match rclone's internal logic: simple path joining
        # BUT rclone paths are complex.
        # Instead, we look for ANY lock file in the directory that matches our paths
        CACHE_DIR="$HOME/.cache/rclone/bisync"
        if [ -d "$CACHE_DIR" ]; then
            # Find lock files containing both path components (rough match)
            # This is safer than trying to recreate the exact hashing logic in bash
            # filename usually looks like: path1..path2.lck
            
            # Sanitize paths for grep
            clean_local=$(basename "$local_path")
            clean_remote=$(basename "$remote_path")
            
            for lck in "$CACHE_DIR"/*"$clean_local"*"$clean_remote"*.lck; do
                if [ -f "$lck" ]; then
                    # Read PID from JSON: {"Session":..., "PID":"12345", ...}
                    # Use grep/sed to extract PID since we might not have jq
                    lck_pid=$(grep -o '"PID":"[0-9]*"' "$lck" | cut -d'"' -f4)
                    
                    if [ -n "$lck_pid" ]; then
                        if ! kill -0 "$lck_pid" 2>/dev/null; then
                            echo "[Auto-Heal] Found stale lock file: $lck (PID $lck_pid is dead)."
                            echo "[Auto-Heal] Removing lock to allow sync to proceed."
                            rm -f "$lck"
                        fi
                    fi
                fi
            done
        fi
    fi
    # ----------------------------------------
    
    # 1. Calculate Resources
    # Get free RAM in MB
    if command -v free >/dev/null; then
        free_ram=$(free -m | awk '/^Mem:/{print $7}')
    else
        free_ram=2000 # Default assumption
    fi
    
    # Get CPU cores
    if command -v nproc >/dev/null; then
        cores=$(nproc)
    else
        cores=4
    fi
    
    # Defaults (Standard)
    # Proton API is sensitive to concurrency. Better to have fewer, faster transfers.
    transfers=4
    checkers=8
    buffer_size="32M"
    
    echo "Dynamic Allocation: Found ${free_ram}MB RAM free, ${cores} Cores."
    
    # Dynamic Mode Selection
    # We strictly enforce MAX 2 transfers to avoid API Throttling (429 Errors).
    # Higher modes now only increase Buffer Size (RAM usage) to speed up large files.
    
    if [ "$free_ram" -gt 16000 ]; then
        echo "Mode: Ultra (High RAM / Max Buffer)"
        transfers=2
        checkers=4
        buffer_size="256M" 
    elif [ "$free_ram" -gt 8000 ]; then
        echo "Mode: High (Aggressive Caching)"
        transfers=2
        checkers=4
        buffer_size="128M"
    elif [ "$free_ram" -gt 4000 ]; then
        echo "Mode: Standard (Safe & Stable)"
        transfers=2
        checkers=4
        buffer_size="64M"
    else
        echo "Mode: Eco (Low RAM / Conservative)"
        transfers=1
        checkers=2
        buffer_size="32M"
    fi
    
    # 2. Build Command
    # Priority: nice -n 15 (Low CPU priority for "Natural Scaling")
    # Disk: ionice -c 3 (Idle) if available
    
    cmd_prefix="nice -n 15"
    if command -v ionice >/dev/null; then
        cmd_prefix="$cmd_prefix ionice -c 3"
    fi
    
    # Adaptive RC Configuration
    # Generate unique port 5500-5600 based on path hash to avoid collisions
    job_hash=$(echo "$local_path" | sha256sum | head -c 4)
    # Convert hex to decimal for offset
    port_offset=$((0x$job_hash % 100))
    rc_port=$((5500 + $port_offset))
    
    # ============================================================
    # PORT CONFLICT RESOLUTION
    # ============================================================
    # Check if port is already in use and kill stale processes
    if ss -tlnp 2>/dev/null | grep -q ":$rc_port " || netstat -tlnp 2>/dev/null | grep -q ":$rc_port "; then
        echo "[Port Conflict] Port $rc_port is in use. Checking for stale rclone processes..."
        
        # Find and kill any rclone process using this port
        stale_pid=$(ps aux | grep "rclone.*--rc-addr=127.0.0.1:$rc_port" | grep -v grep | awk '{print $2}' | head -1)
        if [ -n "$stale_pid" ]; then
            echo "[Port Conflict] Killing stale rclone process (PID: $stale_pid)..."
            kill -9 "$stale_pid" 2>/dev/null || true
            sleep 1
        fi
        
        # If still in use, try next available port
        if ss -tlnp 2>/dev/null | grep -q ":$rc_port " || netstat -tlnp 2>/dev/null | grep -q ":$rc_port "; then
            echo "[Port Conflict] Port still in use, finding alternative..."
            for try_port in $(seq 5500 5650); do
                if ! ss -tlnp 2>/dev/null | grep -q ":$try_port " && ! netstat -tlnp 2>/dev/null | grep -q ":$try_port "; then
                    rc_port=$try_port
                    echo "[Port Conflict] Using alternative port: $rc_port"
                    break
                fi
            done
        fi
    fi
    echo "[RC Port] Using port $rc_port for remote control"
    # ============================================================
    
    # Start Adaptive Monitor in Background
    SCRIPT_DIR=$(dirname "$(readlink -f "$0")")
    MONITOR_SCRIPT="$SCRIPT_DIR/adaptive-monitor.py"
    
    if [ -f "$MONITOR_SCRIPT" ]; then
        echo "Starting Adaptive Monitor on port $rc_port (Max Transfers: $transfers)..."
        # Wait a bit for rclone to start before launching python
        # We use a trap to kill the python monitor when this script exits
        (sleep 2 && python3 "$MONITOR_SCRIPT" --port $rc_port --max-transfers $transfers --init-transfers $transfers >> "$HOME/.config/proton-drive/monitor.log" 2>&1) &
        monitor_pid=$!
        trap "kill $monitor_pid 2>/dev/null" EXIT
    fi

    # Add --rc to enable runtime control
    rclone_flags="--verbose --stats 1s --stats-one-line --transfers $transfers --checkers $checkers --buffer-size $buffer_size --rc --rc-addr=127.0.0.1:$rc_port --rc-no-auth"
    
    # Add robust flags to skip errors and prevent stall
    rclone_flags="$rclone_flags --ignore-errors --retries 3 --retries-sleep 5s"
    
    # ============================================================
    # PROTON DRIVE UPLOAD OPTIMIZATION (without getting rate-limited)
    # ============================================================
    # 
    # Proton Drive has aggressive rate limiting (429 errors).
    # Key strategies:
    # 1. Limit concurrent transfers (already done: max 2)
    # 2. Use --tpslimit to cap API requests per second  
    # 3. Increase --drive-chunk-size to reduce # of API calls per file
    # 4. Use --contimeout to quickly recover from dropped connections
    # 5. Enable --fast-list for fewer API calls during listing
    #
    # TPS Limit: 4 transactions per second = 240/minute (under Proton's limit)
    rclone_flags="$rclone_flags --tpslimit 4"
    
    # Transaction burst: Allow short bursts of 8 requests
    rclone_flags="$rclone_flags --tpslimit-burst 8"
    
    # Faster connection recovery
    rclone_flags="$rclone_flags --contimeout 30s"
    
    # Multi-thread per file: Use 4 threads for files > 8MB (faster large files)
    rclone_flags="$rclone_flags --multi-thread-streams 4 --multi-thread-cutoff 8M"
    
    # Use faster directory listing (caches entire tree in memory)
    rclone_flags="$rclone_flags --fast-list"
    
    # Low-level timeout tuning
    rclone_flags="$rclone_flags --timeout 5m --expect-continue-timeout 3s"
    
    # Avoid updating mod-time after upload (saves API calls, Proton doesn't support it well anyway)
    rclone_flags="$rclone_flags --no-update-modtime"
    
    # Exclude metadata files from sync (they are device-specific)
    rclone_flags="$rclone_flags --exclude .proton-sync-*.json"
    
    echo "[Optimization] TPS Limit: 4/s, Multi-thread: 4 streams, Fast-list: enabled"
    # ============================================================
    
    # --- DELTA SYNC: Use checksums instead of mod-time for better change detection ---
    SETTINGS_FILE="$HOME/.config/proton-drive-linux/settings.json"
    if [ -f "$SETTINGS_FILE" ]; then
        delta_enabled=$(grep -o '"delta_sync"[[:space:]]*:[[:space:]]*[^,}]*' "$SETTINGS_FILE" | grep -o 'true\|false' || echo "true")
        if [ "$delta_enabled" = "true" ]; then
            echo "[Delta Sync] Enabled: Using checksum-based change detection"
            rclone_flags="$rclone_flags --checksum"
        fi
        
        # --- SELECTIVE SYNC: Apply exclusion filters ---
        excluded=$(grep -o '"excluded_folders"[[:space:]]*:[[:space:]]*"[^"]*"' "$SETTINGS_FILE" | cut -d'"' -f4 || echo "")
        if [ -n "$excluded" ]; then
            echo "[Selective Sync] Excluding folders: $excluded"
            # Convert comma-separated list to --exclude flags
            IFS=',' read -ra EXCLUDED_ARRAY <<< "$excluded"
            for folder in "${EXCLUDED_ARRAY[@]}"; do
                if [ -n "$folder" ]; then
                    rclone_flags="$rclone_flags --exclude \"$folder/**\""
                fi
            done
        fi
        
        # --- BANDWIDTH LIMITS: Apply if configured ---
        upload_limit=$(grep -o '"upload_limit"[[:space:]]*:[[:space:]]*[0-9]*' "$SETTINGS_FILE" | grep -o '[0-9]*' || echo "0")
        download_limit=$(grep -o '"download_limit"[[:space:]]*:[[:space:]]*[0-9]*' "$SETTINGS_FILE" | grep -o '[0-9]*' || echo "0")
        
        if [ "$upload_limit" -gt 0 ] 2>/dev/null; then
            bw_limit_kb=$((upload_limit / 1024))
            echo "[Bandwidth] Upload limit: ${bw_limit_kb}KB/s"
            rclone_flags="$rclone_flags --bwlimit ${bw_limit_kb}K"
        fi
        
        # --- BANDWIDTH SCHEDULING: Time-based limits ---
        sched_enabled=$(grep -o '"bw_scheduling_enabled"[[:space:]]*:[[:space:]]*[^,}]*' "$SETTINGS_FILE" | grep -o 'true\|false' || echo "false")
        if [ "$sched_enabled" = "true" ]; then
            work_start=$(grep -o '"work_hour_start"[[:space:]]*:[[:space:]]*[0-9]*' "$SETTINGS_FILE" | grep -o '[0-9]*' || echo "9")
            work_end=$(grep -o '"work_hour_end"[[:space:]]*:[[:space:]]*[0-9]*' "$SETTINGS_FILE" | grep -o '[0-9]*' || echo "17")
            work_limit=$(grep -o '"work_hour_limit_kb"[[:space:]]*:[[:space:]]*[0-9]*' "$SETTINGS_FILE" | grep -o '[0-9]*' || echo "500")
            
            current_hour=$(date +%H)
            if [ "$current_hour" -ge "$work_start" ] && [ "$current_hour" -lt "$work_end" ]; then
                echo "[Bandwidth Scheduling] Work hours active (${work_start}:00 - ${work_end}:00), limiting to ${work_limit}KB/s"
                rclone_flags="$rclone_flags --bwlimit ${work_limit}K"
            fi
        fi
        
        # ============================================================
        # SYNC SAFETY FEATURES
        # ============================================================
        
        # --- MAX DELETE PROTECTION: Abort if too many files deleted ---
        # Note: For bisync, --max-delete takes a percentage VALUE (e.g., 50 means 50%)
        # For regular sync, --max-delete takes a file count. We handle both cases.
        max_delete_percent=$(grep -o '"max_delete_percent"[[:space:]]*:[[:space:]]*[0-9]*' "$SETTINGS_FILE" | grep -o '[0-9]*' || echo "50")
        if [ -n "$max_delete_percent" ] && [ "$max_delete_percent" -gt 0 ]; then
            echo "[Safety] Max delete protection: ${max_delete_percent}%"
            if [ "$sync_type" = "bisync" ]; then
                # bisync: --max-delete takes a percentage NUMBER (not with % suffix)
                rclone_flags="$rclone_flags --max-delete $max_delete_percent"
            fi
            # Note: For regular 'sync' command, --max-delete takes file count, not percentage
            # We skip it for non-bisync as the semantics are different
        fi
        
        # --- CONFLICT RESOLUTION: How to handle file conflicts ---
        conflict_resolve=$(grep -o '"conflict_resolve"[[:space:]]*:[[:space:]]*"[^"]*"' "$SETTINGS_FILE" | cut -d'"' -f4 || echo "newer")
        conflict_behavior=$(grep -o '"conflict_behavior"[[:space:]]*:[[:space:]]*"[^"]*"' "$SETTINGS_FILE" | cut -d'"' -f4 || echo "wait")
        
        if [ "$sync_type" = "bisync" ]; then
            case "$conflict_resolve" in
                "newer")
                    echo "[Conflict Resolution] Mode: Newer file wins"
                    rclone_flags="$rclone_flags --conflict-resolve newer"
                    ;;
                "larger")
                    echo "[Conflict Resolution] Mode: Larger file wins"
                    rclone_flags="$rclone_flags --conflict-resolve larger"
                    ;;
                "path1")
                    echo "[Conflict Resolution] Mode: Local file wins"
                    rclone_flags="$rclone_flags --conflict-resolve path1"
                    ;;
                "path2")
                    echo "[Conflict Resolution] Mode: Remote file wins"
                    rclone_flags="$rclone_flags --conflict-resolve path2"
                    ;;
                "ask")
                    # Don't auto-resolve - will be handled by app after sync
                    echo "[Conflict Resolution] Mode: Ask user"
                    rclone_flags="$rclone_flags --conflict-resolve none"
                    ;;
                *)
                    # Default: keep both files
                    echo "[Conflict Resolution] Mode: Keep both (numbered)"
                    rclone_flags="$rclone_flags --conflict-resolve none"
                    ;;
            esac
            
            # Conflict loser handling: rename with timestamp suffix
            rclone_flags="$rclone_flags --conflict-loser num"
            rclone_flags="$rclone_flags --conflict-suffix _conflict-{{.Time}}"
            
            # Lock expiry: auto-remove stale locks after 2 hours
            rclone_flags="$rclone_flags --max-lock 2h"
        fi
        
        # --- FILE VERSIONING: Keep backups of overwritten files ---
        versioning_enabled=$(grep -o '"enable_versioning"[[:space:]]*:[[:space:]]*[^,}]*' "$SETTINGS_FILE" | grep -o 'true\|false' || echo "false")
        if [ "$versioning_enabled" = "true" ]; then
            # Create backup directory in local sync folder
            backup_dir="${local_path}/.rclone_backups"
            mkdir -p "$backup_dir" 2>/dev/null || true
            echo "[Versioning] Enabled: Backups stored in $backup_dir"
            rclone_flags="$rclone_flags --backup-dir \"$backup_dir\""
        fi
        
        # --- CHECK ACCESS: Verify paths are accessible before sync ---
        check_access_enabled=$(grep -o '"check_access_enabled"[[:space:]]*:[[:space:]]*[^,}]*' "$SETTINGS_FILE" | grep -o 'true\|false' || echo "false")
        if [ "$check_access_enabled" = "true" ] && [ "$sync_type" = "bisync" ]; then
            echo "[Safety] Check access verification enabled"
            rclone_flags="$rclone_flags --check-access"
            # Create RCLONE_TEST file if it doesn't exist
            test_file="${local_path}/RCLONE_TEST"
            if [ ! -f "$test_file" ]; then
                echo "rclone test file - do not delete" > "$test_file" 2>/dev/null || true
            fi
        fi
        
        echo "[Safety] Max-delete: ${max_delete_percent}%, Lock expiry: 2h"
        # ============================================================
    fi

    if [ "$resync" = "true" ] || [ "$resync" = "--resync" ]; then
        resync_flag="--resync"
    else
        resync_flag=""
    fi
    
    # Add job-specific excludes to rclone flags
    if [ -n "$job_excludes" ]; then
        rclone_flags="$rclone_flags $job_excludes"
    fi
    
    if [ "$sync_type" = "bisync" ]; then
        final_cmd="$cmd_prefix $RCLONE_CMD bisync \"$local_path\" \"$remote_path\" $resync_flag $rclone_flags"
    else
        final_cmd="$cmd_prefix $RCLONE_CMD sync \"$local_path\" \"$remote_path\" $rclone_flags"
    fi
    
    echo "Executing: $final_cmd"
    
    # Execute in background to capture PID for graceful shutdown
    set +e  # Don't exit on error
    eval "$final_cmd" > /tmp/rclone_output_$$.txt 2>&1 &
    RCLONE_PID=$!
    echo "[Sync] rclone started with PID: $RCLONE_PID"
    
    # Wait for rclone to complete
    wait $RCLONE_PID
    exit_code=$?
    
    # Read and display output
    output=$(cat /tmp/rclone_output_$$.txt 2>/dev/null || echo "")
    rm -f /tmp/rclone_output_$$.txt
    echo "$output"
    RCLONE_PID=""
    set -e
    
    # --- AUTO-RECOVERY: Retry with --resync if bisync failed ---
    if [ "$sync_type" = "bisync" ] && [ $exit_code -ne 0 ]; then
        if echo "$output" | grep -q "Must run --resync to recover"; then
            echo "[Auto-Recovery] Bisync failed, retrying with --resync..."
            resync_flag="--resync"
            final_cmd="$cmd_prefix $RCLONE_CMD bisync \"$local_path\" \"$remote_path\" $resync_flag $rclone_flags"
            echo "Executing: $final_cmd"
            
            # Execute retry with PID tracking
            eval "$final_cmd" > /tmp/rclone_output_$$.txt 2>&1 &
            RCLONE_PID=$!
            wait $RCLONE_PID
            output=$(cat /tmp/rclone_output_$$.txt 2>/dev/null || echo "")
            rm -f /tmp/rclone_output_$$.txt
            echo "$output"
            RCLONE_PID=""
        fi
    fi
}

case "$1" in
    list)
        list_jobs
        ;;
    add)
        add_job "${@:2}"
        ;;
    create-with-id)
        create_with_id "${@:2}"
        ;;
    edit)
        edit_job "${@:2}"
        ;;
    remove)
        remove_job "$2"
        ;;
    sync)
        trigger_job "$2"
        ;;
    force-resync)
        job_id="$2"
        if [ -f "$JOB_DIR/$job_id.conf" ]; then
            source "$JOB_DIR/$job_id.conf"
            # run_dynamic_job takes: local remote type resync
            run_dynamic_job "$LOCAL_PATH" "$REMOTE_PATH" "$SYNC_TYPE" "--resync"
        else
            echo "Job not found."
            exit 1
        fi
        ;;
    run)
        run_dynamic_job "${@:2}"
        ;;
    *)
        echo "Usage: $0 {list|add|edit|remove|sync|force-resync|run} ..."
        exit 1
        ;;
esac
