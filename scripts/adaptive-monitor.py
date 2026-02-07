#!/usr/bin/env python3
"""
Proton Drive Adaptive Speed Manager

This script dynamically adjusts rclone transfer speed based on:
1. Error rates (429 rate limits, 502 server errors)
2. Successful transfer streaks
3. Time-based backoff and recovery

Key strategy: Start slow, gradually increase, immediately back off on errors.
"""
import sys
import time
import requests
import argparse
import signal
import json

# Proton Drive specific limits
DEFAULT_TRANSFERS = 2  # Very conservative start
MIN_TRANSFERS = 1
MAX_TRANSFERS = 4  # Proton doesn't like more than this
MIN_TPS = 2  # Transactions per second
MAX_TPS = 8

# Backoff multipliers
ERROR_BACKOFF = 0.5  # Cut in half on error
SEVERE_ERROR_BACKOFF = 0.25  # Quarter on severe errors (429)

def get_stats(base_url):
    try:
        r = requests.post(f"{base_url}/core/stats", timeout=5)
        if r.status_code == 200:
            return r.json()
    except:
        pass
    return None

def set_transfers(base_url, count):
    try:
        payload = {"main": {"Transfers": int(count)}}
        requests.post(f"{base_url}/options/set", json=payload, timeout=5)
        return True
    except:
        return False

def set_tps_limit(base_url, tps):
    """Set transactions per second limit"""
    try:
        payload = {"main": {"TPSLimit": float(tps), "TPSLimitBurst": int(tps * 2)}}
        requests.post(f"{base_url}/options/set", json=payload, timeout=5)
        return True
    except:
        return False

def is_rate_limit_error(last_error):
    """Check if error is a rate limit (429) or server error (502/503)"""
    if not last_error:
        return False, False
    error_str = str(last_error).lower()
    is_429 = "429" in error_str or "too many" in error_str or "rate limit" in error_str
    is_5xx = "502" in error_str or "503" in error_str or "bad gateway" in error_str
    return is_429, is_5xx

def main():
    parser = argparse.ArgumentParser(description="Proton Drive Adaptive Speed Manager")
    parser.add_argument("--port", type=int, required=True, help="rclone RC port")
    parser.add_argument("--max-transfers", type=int, default=MAX_TRANSFERS)
    parser.add_argument("--init-transfers", type=int, default=DEFAULT_TRANSFERS)
    args = parser.parse_args()

    base_url = f"http://127.0.0.1:{args.port}"
    
    # Start at minimum safe speed
    current_transfers = max(MIN_TRANSFERS, min(args.init_transfers, args.max_transfers))
    current_tps = MIN_TPS
    
    # Set initial conservative speed
    set_transfers(base_url, current_transfers)
    set_tps_limit(base_url, current_tps)
    print(f"[Monitor] Starting with {current_transfers} transfers, {current_tps} TPS")

    last_error_count = 0
    last_error_msg = ""
    stable_intervals = 0
    bytes_last = 0
    stuck_count = 0
    
    # Wait for rclone to start
    print(f"[Monitor] Waiting for rclone on port {args.port}...")
    for _ in range(30):
        stats = get_stats(base_url)
        if stats:
            print("[Monitor] Connected to rclone.")
            last_error_count = stats.get("errors", 0)
            bytes_last = stats.get("bytes", 0)
            break
        time.sleep(1)
    else:
        print("[Monitor] Timeout waiting for rclone. Exiting.")
        return
    
    while True:
        time.sleep(10)
        stats = get_stats(base_url)
        if not stats:
            print("[Monitor] Rclone unavailable. Exiting.")
            break
            
        curr_errors = stats.get("errors", 0)
        curr_bytes = stats.get("bytes", 0)
        last_error_msg = stats.get("lastError", "")
        speed = stats.get("speed", 0)
        
        # Check for new errors
        new_errors = curr_errors - last_error_count
        if new_errors > 0:
            is_429, is_5xx = is_rate_limit_error(last_error_msg)
            
            if is_429:
                # Rate limited! Aggressive backoff
                print(f"[Monitor] RATE LIMITED (429)! Aggressive backoff.")
                new_transfers = max(MIN_TRANSFERS, int(current_transfers * SEVERE_ERROR_BACKOFF))
                new_tps = max(MIN_TPS, current_tps * SEVERE_ERROR_BACKOFF)
                
                # Also pause briefly to let Proton's rate limiter reset
                time.sleep(30)
                
            elif is_5xx:
                # Server error - moderate backoff
                print(f"[Monitor] Server error (5xx). Moderate backoff.")
                new_transfers = max(MIN_TRANSFERS, int(current_transfers * ERROR_BACKOFF))
                new_tps = max(MIN_TPS, current_tps * ERROR_BACKOFF)
                
            else:
                # Other error - gentle backoff
                print(f"[Monitor] Error detected: {last_error_msg[:50]}...")
                new_transfers = max(MIN_TRANSFERS, current_transfers - 1)
                new_tps = max(MIN_TPS, current_tps - 0.5)
            
            # Apply backoff
            if new_transfers != current_transfers or new_tps != current_tps:
                set_transfers(base_url, new_transfers)
                set_tps_limit(base_url, new_tps)
                current_transfers = new_transfers
                current_tps = new_tps
                print(f"[Monitor] Backed off to {current_transfers} transfers, {current_tps:.1f} TPS")
            
            last_error_count = curr_errors
            stable_intervals = 0
            stuck_count = 0
            
        else:
            # No new errors
            stable_intervals += 1
            
            # Check if we're stuck (no progress)
            if curr_bytes == bytes_last and speed < 100:
                stuck_count += 1
                if stuck_count > 6:  # Stuck for 1 minute
                    print("[Monitor] Progress stalled. Reducing load.")
                    new_transfers = max(MIN_TRANSFERS, current_transfers - 1)
                    set_transfers(base_url, new_transfers)
                    current_transfers = new_transfers
                    stuck_count = 0
            else:
                stuck_count = 0
            
            bytes_last = curr_bytes
            
            # Gradual speed increase after sustained stability
            if stable_intervals >= 12:  # 2 minutes stable
                # Slowly increase
                if current_transfers < args.max_transfers:
                    new_transfers = current_transfers + 1
                    if set_transfers(base_url, new_transfers):
                        current_transfers = new_transfers
                        print(f"[Monitor] Stable. Increasing to {current_transfers} transfers.")
                        stable_intervals = 0
                        
                elif current_tps < MAX_TPS:
                    new_tps = min(MAX_TPS, current_tps + 0.5)
                    if set_tps_limit(base_url, new_tps):
                        current_tps = new_tps
                        print(f"[Monitor] Stable. Increasing TPS to {current_tps:.1f}")
                        stable_intervals = 0

if __name__ == "__main__":
    main()
