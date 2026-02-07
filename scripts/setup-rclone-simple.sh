#!/bin/bash
# Proton Drive Linux - Simple Rclone Setup
# Configures rclone for Proton Drive folder sync
# Uses username/password auth (2FA supported via config wizard if needed)

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

RCLONE_CONFIG="$HOME/.config/rclone/rclone.conf"
REMOTE_NAME="${PROTON_REMOTE_NAME:-proton}"

# Check prerequisites
check_prerequisites() {
    if ! command -v rclone &> /dev/null; then
        print_error "rclone is not installed."
        echo "Installing rclone..."
        curl https://rclone.org/install.sh | sudo bash || {
            print_error "Failed to install rclone"
            exit 1
        }
    fi
    print_success "rclone is installed: $(rclone version | head -1)"
}

# Configure rclone with provided credentials
configure_rclone() {
    print_status "Configuring rclone for Proton Drive..."
    
    # Get username from environment or prompt
    if [ -z "$PROTON_USERNAME" ]; then
        echo ""
        read -p "Proton Username/Email: " PROTON_USERNAME
    fi
    
    if [ -z "$PROTON_USERNAME" ]; then
        print_error "Username is required"
        return 1
    fi
    
    # Get password from environment or prompt
    if [ -z "$PROTON_PASSWORD" ]; then
        echo ""
        read -s -p "Proton Password: " PROTON_PASSWORD
        echo ""
    fi
    
    if [ -z "$PROTON_PASSWORD" ]; then
        print_error "Password is required"
        return 1
    fi
    
    # Obscure the password for rclone config
    OBSCURED_PASSWORD=$(echo "$PROTON_PASSWORD" | rclone obscure -)
    if [ -z "$OBSCURED_PASSWORD" ]; then
        print_error "Failed to obscure password"
        return 1
    fi
    
    # Create rclone config directory
    mkdir -p "$(dirname "$RCLONE_CONFIG")"
    
    # Remove existing proton remote if it exists
    rclone config delete "$REMOTE_NAME" 2>/dev/null || true
    
    # Create rclone config
    # Note: This creates a basic config. If 2FA is enabled, the first connection
    # attempt will trigger the 2FA prompt in the terminal.
    cat >> "$RCLONE_CONFIG" <<EOF

[$REMOTE_NAME]
type = protondrive
username = $PROTON_USERNAME
password = $OBSCURED_PASSWORD
EOF

    chmod 600 "$RCLONE_CONFIG"
    print_success "Rclone config written to $RCLONE_CONFIG"
}

# Test connection
test_connection() {
    print_status "Testing Proton Drive connection..."
    
    # This may prompt for 2FA if enabled
    if timeout 60 rclone lsd "$REMOTE_NAME:" 2>&1; then
        print_success "Proton Drive connection successful!"
        echo ""
        echo "Your Proton Drive folders:"
        rclone lsd "$REMOTE_NAME:" 2>/dev/null | head -10
        return 0
    else
        print_error "Connection test failed"
        echo ""
        echo "Possible issues:"
        echo "  • Wrong username/password"
        echo "  • 2FA required (will prompt on next sync attempt)"
        echo "  • Network issues"
        echo "  • CAPTCHA required (try rclone config manually)"
        echo ""
        echo "For CAPTCHA issues, run: rclone config"
        echo "and follow the interactive prompts."
        return 1
    fi
}

main() {
    echo ""
    echo "╔═══════════════════════════════════════════════════════════╗"
    echo "║   Proton Drive Linux - Folder Sync Setup                  ║"
    echo "╚═══════════════════════════════════════════════════════════╝"
    echo ""
    
    check_prerequisites
    configure_rclone
    
    echo ""
    if test_connection; then
        echo ""
        print_success "Setup complete!"
        echo ""
        echo "You can now add sync folders in the Sync Manager."
    else
        print_warning "Connection test failed, but config was saved."
        echo "You may need to complete 2FA on first sync, or run 'rclone config' for CAPTCHA."
    fi
}

main "$@"
