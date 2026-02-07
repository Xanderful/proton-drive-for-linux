#!/bin/bash
# Proton Drive Linux - Rclone Setup Script
# Sets up rclone with Proton Drive support for folder sync
# Strategy: Use standard interactive browser authentication (Celeste-style) for maximum reliability.

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_status() { echo -e "${BLUE}[*]${NC} $1"; }
print_success() { echo -e "${GREEN}[✓]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[!]${NC} $1"; }
print_error() { echo -e "${RED}[✗]${NC} $1"; }

# Check if running as root
if [ "$EUID" -eq 0 ]; then
    print_error "Please do not run this script as root"
    exit 1
fi

# Minimum rclone version required (Proton Drive support)
MIN_RCLONE_VERSION="1.64.0"

# Check rclone version
check_rclone_version() {
    if ! command -v rclone &> /dev/null; then
        return 1
    fi
    
    local version=$(rclone version | head -1 | grep -oP 'v\K[0-9]+\.[0-9]+\.[0-9]+')
    
    if [ "$(printf '%s\n' "$MIN_RCLONE_VERSION" "$version" | sort -V | head -1)" = "$MIN_RCLONE_VERSION" ]; then
        echo "$version"
        return 0
    else
        return 1
    fi
}

# Check if Proton Drive remote is configured and working
check_proton_remote() {
    if rclone listremotes | grep -q "^proton:"; then
        if OUTPUT=$(rclone lsf proton: --max-depth 1 -vv 2>&1); then
            return 0
        fi
    fi
    return 1
}

main() {
    echo ""
    echo "╔═══════════════════════════════════════════════════════════╗"
    echo "║         Proton Drive Linux - Rclone Setup                 ║"
    echo "╚═══════════════════════════════════════════════════════════╝"
    echo ""
    
    # Prereq checks
    if ! command -v rclone &> /dev/null; then
         print_error "Rclone not found. Please install rclone."
         exit 1
    fi
    
    if check_proton_remote; then
        print_success "Proton Drive remote (proton:) is already configured and working!"
        echo ""
        echo "Next steps:"
        echo "  1. Run: ./scripts/setup-sync-service.sh"
        exit 0
    fi

    echo "You will now need to configure your Proton Drive credentials."
    echo "This script will launch the browser for secure authentication."
    echo ""
    
    local rclone_username=""
    read -p "Proton Username: " rclone_username
    
    echo "Note: Your password is required to decrypt your private keys."
    read -s -p "Proton Password: " rclone_password
    echo ""
    echo ""

    # Obscure password securely
    local obscured_pass
    while true; do
        if ! obscured_pass=$(rclone obscure "$rclone_password"); then
            print_error "Failed to obscure password."
            exit 1
        fi
        if [[ "$obscured_pass" != -* ]]; then
            break
        fi
    done

    echo "[*] Launching Rclone Configuration Wizard..."
    echo "    1. A browser window will open."
    echo "    2. Authorize the application."
    echo "    3. Return here."
    
    # Remove existing remote to prevent conflicts
    rclone config delete proton > /dev/null 2>&1 || true

    # Run 'rclone config create' with pre-filled credentials.
    # We pipe 'n' (No, I don't have a token) and 'y' (Yes, use browser) to automate the prompts.
    if printf "n\ny\n" | rclone config create proton protondrive username "$rclone_username" password "$obscured_pass"; then
        echo ""
        print_status "Verifying connection..."
        if check_proton_remote; then
            print_success "Proton Drive remote configured successfully!"
            echo ""
            echo "Next steps:"
            echo "  1. Run: ./scripts/setup-sync-service.sh"
            echo "  2. Your Proton Drive will be mounted at ~/ProtonDrive"
            exit 0
        fi
    fi

    # Fallback if automation fails
    print_error "Automated setup failed."
    echo ""
    echo "Please perform the following steps MANUALLY in this terminal:"
    echo "1. Run: rclone config"
    echo "2. Press 'n' (New remote)"
    echo "3. Name: proton"
    echo "4. Type: protondrive"
    echo "5. Username: $rclone_username"
    echo "6. Password: (Enter your password, confirm it)"
    echo "7. 2FA: (Values are empty if not set, else enter code)"
    echo "8. 'Already have a token?': n"
    echo "9. 'Use web browser?': y"
    echo "10. Log in via the browser window that opens."
    echo "11. 'Keep this remote?': y"
    echo "12. 'Quit': q"
    echo ""
    exit 1
}

main "$@"
