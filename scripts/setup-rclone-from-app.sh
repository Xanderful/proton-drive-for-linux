#!/bin/bash
# Proton Drive Linux - Rclone Setup from App Session
# Extracts session tokens from the Proton Drive app and configures rclone
# This bypasses CAPTCHA by using browser-authenticated session tokens

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

COOKIE_FILE="$HOME/.local/share/proton-drive-linux/cookies.txt"
RCLONE_CONFIG="$HOME/.config/rclone/rclone.conf"

# Check if running as root
if [ "$EUID" -eq 0 ]; then
    print_error "Please do not run this script as root"
    exit 1
fi

# Check prerequisites
check_prerequisites() {
    if ! command -v rclone &> /dev/null; then
        print_error "rclone is not installed."
        echo "Install with: sudo pacman -S rclone"
        exit 1
    fi
    
    if [ ! -f "$COOKIE_FILE" ]; then
        print_error "App cookies not found. Please log into Proton Drive via the app first."
        echo ""
        echo "Steps:"
        echo "  1. Run the Proton Drive app"
        echo "  2. Log in with your Proton account"
        echo "  3. After successful login, run this script again"
        exit 1
    fi
}

# Extract tokens from cookie file
extract_tokens() {
    print_status "Extracting session tokens from app cookies..."
    
    # Find the drive.proton.me AUTH token (most recent one)
    local auth_line=$(grep "drive.proton.me.*AUTH-" "$COOKIE_FILE" | tail -1)
    
    if [ -z "$auth_line" ]; then
        # Also check account.proton.me
        auth_line=$(grep "account.proton.me.*AUTH-" "$COOKIE_FILE" | tail -1)
    fi
    
    if [ -z "$auth_line" ]; then
        print_error "No AUTH token found. Please log into Proton Drive via the app first."
        return 1
    fi
    
    # Extract UID from cookie name (AUTH-{uid})
    # Note: Use PROTON_UID instead of UID (UID is readonly in bash)
    PROTON_UID=$(echo "$auth_line" | grep -oP 'AUTH-\K[a-z0-9]+')
    
    # Extract access token (last field)
    ACCESS_TOKEN=$(echo "$auth_line" | awk -F'\t' '{print $NF}' | tr -d '\r\n')
    
    # Find corresponding REFRESH token
    local refresh_line=$(grep "drive.proton.me.*REFRESH-$PROTON_UID" "$COOKIE_FILE" | tail -1)
    
    if [ -z "$refresh_line" ]; then
        refresh_line=$(grep "account.proton.me.*REFRESH-$PROTON_UID" "$COOKIE_FILE" | tail -1)
    fi
    
    if [ -n "$refresh_line" ]; then
        # URL-decode and extract RefreshToken from JSON
        local refresh_json=$(echo "$refresh_line" | awk -F'\t' '{print $NF}' | python3 -c "import sys, urllib.parse; print(urllib.parse.unquote(sys.stdin.read()))" 2>/dev/null)
        REFRESH_TOKEN=$(echo "$refresh_json" | python3 -c "import sys, json; print(json.loads(sys.stdin.read()).get('RefreshToken', ''))" 2>/dev/null || echo "")
    fi
    
    if [ -z "$PROTON_UID" ] || [ -z "$ACCESS_TOKEN" ]; then
        print_error "Failed to extract required tokens"
        return 1
    fi
    
    print_success "Found session tokens for UID: $PROTON_UID"
    echo "  Access Token: ${ACCESS_TOKEN:0:10}..."
    [ -n "$REFRESH_TOKEN" ] && echo "  Refresh Token: ${REFRESH_TOKEN:0:10}..."
    
    return 0
}

# Try to get username from Proton API using session token
get_username_from_session() {
    print_status "Fetching username from Proton session..."
    
    # Use the access token to query the users endpoint
    local response=$(curl -s -H "Authorization: Bearer $ACCESS_TOKEN" \
                          -H "x-pm-uid: $PROTON_UID" \
                          "https://account.proton.me/api/core/v4/users" 2>/dev/null)
    
    if [ -n "$response" ]; then
        # Try to extract Name field from response
        PROTON_USERNAME=$(echo "$response" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    if 'User' in data:
        print(data['User'].get('Name', data['User'].get('Email', '')).split('@')[0])
    else:
        print('')
except:
    print('')
" 2>/dev/null)
    fi
    
    if [ -n "$PROTON_USERNAME" ]; then
        print_success "Found username: $PROTON_USERNAME"
        return 0
    else
        print_warning "Could not auto-detect username from session"
        return 1
    fi
}

# Configure rclone with tokens
configure_rclone() {
    print_status "Configuring rclone with session tokens..."
    
    # Get username - try environment, then session API, then prompt
    if [ -z "$PROTON_USERNAME" ]; then
        # Try to get from session
        get_username_from_session || true
    fi
    
    # If still empty, prompt
    if [ -z "$PROTON_USERNAME" ]; then
        echo ""
        read -p "Proton Username/Email: " PROTON_USERNAME
    fi
    
    if [ -z "$PROTON_USERNAME" ]; then
        print_error "Username is required"
        return 1
    fi
    
    # Prompt for password (required by rclone protondrive backend)
    echo ""
    if [ -z "$PROTON_PASSWORD" ]; then
        read -s -p "Proton Password (required for encryption keys): " PROTON_PASSWORD
        echo ""
    else
        echo "Using password provided via environment variable"
    fi
    
    # Obscure the password
    OBSCURED_PASSWORD=$(echo "$PROTON_PASSWORD" | rclone obscure -)
    if [ -z "$OBSCURED_PASSWORD" ]; then
        print_error "Failed to obscure password"
        return 1
    fi
    
    # Create rclone config directory
    mkdir -p "$(dirname "$RCLONE_CONFIG")"
    
    # Remove existing proton remote
    rclone config delete proton 2>/dev/null || true
    
    # Create rclone config with tokens AND password
    # The tokens help resume session without CAPTCHA
    cat > "$RCLONE_CONFIG" <<EOF
[proton]
type = protondrive
username = $PROTON_USERNAME
password = $OBSCURED_PASSWORD
client_uid = $PROTON_UID
client_access_token = $ACCESS_TOKEN
EOF

    if [ -n "$REFRESH_TOKEN" ]; then
        echo "client_refresh_token = $REFRESH_TOKEN" >> "$RCLONE_CONFIG"
    fi
    
    chmod 600 "$RCLONE_CONFIG"
    print_success "Rclone config written to $RCLONE_CONFIG"
}

# Test connection
test_connection() {
    print_status "Testing Proton Drive connection..."
    
    if rclone lsd proton: 2>&1; then
        print_success "Proton Drive connection successful!"
        echo ""
        echo "Your Proton Drive folders:"
        rclone lsd proton: 2>/dev/null | head -10
        return 0
    else
        print_error "Connection test failed"
        echo ""
        echo "The tokens may have expired. Please:"
        echo "  1. Open the Proton Drive app"
        echo "  2. Log out and log back in"
        echo "  3. Run this script again"
        return 1
    fi
}

main() {
    echo ""
    echo "╔═══════════════════════════════════════════════════════════╗"
    echo "║   Proton Drive Linux - Browser-Based Auth Setup           ║"
    echo "╚═══════════════════════════════════════════════════════════╝"
    echo ""
    echo "This script extracts session tokens from the Proton Drive app"
    echo "to configure rclone without CAPTCHA issues."
    echo ""
    
    check_prerequisites
    
    if ! extract_tokens; then
        exit 1
    fi
    
    configure_rclone
    
    echo ""
    if test_connection; then
        echo ""
        print_success "Setup complete!"
        echo ""
        echo "Next steps:"
        echo "  1. Run: ./scripts/setup-sync-service.sh"
        echo "  2. Your Proton Drive will be mounted at ~/ProtonDrive"
    fi
}

main "$@"
