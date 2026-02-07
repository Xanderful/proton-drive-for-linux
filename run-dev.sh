#!/bin/bash
# Clean launcher for Proton Drive - removes snap/flatpak environment pollution
# Use this instead of running the binary directly from VS Code's snap terminal

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY="$SCRIPT_DIR/src-native/build/proton-drive"

if [ ! -f "$BINARY" ]; then
    echo "Error: Binary not found at $BINARY"
    echo "Run: make dev"
    exit 1
fi

# Clean all snap/flatpak environment pollution
unset SNAP
unset SNAP_NAME
unset SNAP_INSTANCE_NAME
unset SNAP_ARCH
unset SNAP_COMMON
unset SNAP_DATA
unset SNAP_LIBRARY_PATH
unset SNAP_USER_COMMON
unset SNAP_USER_DATA
unset SNAP_LAUNCHER_ARCH_TRIPLET
unset BAMF_DESKTOP_FILE_HINT
unset GDK_PIXBUF_MODULE_FILE
unset GDK_PIXBUF_MODULEDIR

# Clean LD_LIBRARY_PATH from snap paths
if [ -n "${LD_LIBRARY_PATH:-}" ]; then
    NEW_LD=""
    IFS=':'
    for path in $LD_LIBRARY_PATH; do
        case "$path" in
            /snap/*|*/snap/*|/var/lib/flatpak/*|/var/lib/snapd/*|*/.local/share/flatpak/*)
                # Skip contaminated paths
                ;;
            *)
                if [ -n "$NEW_LD" ]; then
                    NEW_LD="$NEW_LD:$path"
                else
                    NEW_LD="$path"
                fi
                ;;
        esac
    done
    export LD_LIBRARY_PATH="$NEW_LD"
fi

# Clean LD_PRELOAD
if [ -n "${LD_PRELOAD:-}" ]; then
    case "${LD_PRELOAD:-}" in
        *snap*|*flatpak*)
            unset LD_PRELOAD
            ;;
    esac
fi

# Clean GTK/GIO paths
unset GTK_PATH
unset GTK_EXE_PREFIX
unset GIO_MODULE_DIR

# Clean GStreamer paths
if [ -n "${GST_PLUGIN_PATH:-}" ]; then
    unset GST_PLUGIN_PATH
fi
if [ -n "${GST_PLUGIN_SYSTEM_PATH:-}" ]; then
    unset GST_PLUGIN_SYSTEM_PATH
fi

# Clean XDG paths from snap
if [ -n "${XDG_DATA_DIRS:-}" ]; then
    NEW_XDG=""
    IFS=':'
    for path in $XDG_DATA_DIRS; do
        case "$path" in
            /snap/*|*/snap/*|/var/lib/flatpak/*|/var/lib/snapd/*)
                ;;
            *)
                if [ -n "$NEW_XDG" ]; then
                    NEW_XDG="$NEW_XDG:$path"
                else
                    NEW_XDG="$path"
                fi
                ;;
        esac
    done
    export XDG_DATA_DIRS="$NEW_XDG"
fi

# Use system WebKit, not snap's
export WEBKIT_DISABLE_COMPOSITING_MODE=1

# Launch with completely clean environment using env -i
echo "🚀 Starting Proton Drive (isolated from snap)..."
exec env -i \
    HOME="$HOME" \
    USER="$USER" \
    PATH="/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin" \
    DISPLAY="${DISPLAY:-:0}" \
    XAUTHORITY="${XAUTHORITY:-$HOME/.Xauthority}" \
    DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-}" \
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" \
    LANG="${LANG:-en_US.UTF-8}" \
    "$BINARY" "$@"
