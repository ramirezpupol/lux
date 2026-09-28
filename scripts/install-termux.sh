#!/data/data/com.termux/files/usr/bin/bash
#
# install-termux.sh — one-line installer for Lux on Android (Termux).
#
# Usage:
#   curl -sSL https://raw.githubusercontent.com/ramirezpupol/lux/main/scripts/install-termux.sh | bash
#
# What it does:
#   1. Ensures dpkg and curl are present (pkg install)
#   2. Detects the device architecture (aarch64 / arm / x86_64)
#   3. Downloads the matching .deb from the latest GitHub Release
#   4. Installs it with dpkg -i
#   5. Runs `lux --help` to verify the installation
#
# Repository override:
#   LUX_REPO="user/lux" ./install-termux.sh

set -euo pipefail

REPO="${LUX_REPO:-ramirezpupol/lux}"
API="https://api.github.com/repos/${REPO}/releases/latest"

log()  { printf '\033[1;34m[*]\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m[✓]\033[0m %s\n' "$*"; }
err()  { printf '\033[1;31m[!]\033[0m %s\n' "$*" >&2; }

# ------------------------------------------------------------------
# 1. Verify we are inside Termux
# ------------------------------------------------------------------
if [ -z "${TERMUX_VERSION:-}" ] && [ ! -d "/data/data/com.termux/files/usr" ]; then
    err "This script must be run inside Termux (Android)."
    exit 1
fi

# ------------------------------------------------------------------
# 2. Ensure required packages
# ------------------------------------------------------------------
log "Ensuring dpkg and curl are installed..."
pkg install -y dpkg curl > /dev/null 2>&1 || true

# ------------------------------------------------------------------
# 3. Detect architecture (Termux naming)
# ------------------------------------------------------------------
ARCH="$(dpkg --print-architecture)"
case "$ARCH" in
    aarch64|arm|x86_64|i686) ;;
    *)
        err "Unsupported architecture: $ARCH"
        exit 1
        ;;
esac
log "Detected architecture: $ARCH"

# ------------------------------------------------------------------
# 4. Fetch the latest release .deb URL
# ------------------------------------------------------------------
log "Querying latest release from GitHub..."
ASSETS_JSON="$(curl -sSL "$API")"

DEB_URL="$(printf '%s' "$ASSETS_JSON" \
    | grep -oE "https://[^\"]*lux-termux-${ARCH}\.deb" \
    | head -1)"

if [ -z "$DEB_URL" ]; then
    err "No lux-termux-${ARCH}.deb found in the latest release."
    err "Check https://github.com/${REPO}/releases"
    exit 1
fi
log "Found: $DEB_URL"

# ------------------------------------------------------------------
# 5. Download
# ------------------------------------------------------------------
TMP_DEB="$(mktemp --suffix=.deb)"
trap 'rm -f "$TMP_DEB"' EXIT
log "Downloading..."
curl -sSL -o "$TMP_DEB" "$DEB_URL"

# ------------------------------------------------------------------
# 6. Install
# ------------------------------------------------------------------
log "Installing package..."
if ! dpkg -i "$TMP_DEB"; then
    err "dpkg -i failed; trying to fix dependencies..."
    apt-get install -f -y || true
    dpkg -i "$TMP_DEB"
fi

# ------------------------------------------------------------------
# 7. Verify
# ------------------------------------------------------------------
if command -v lux > /dev/null 2>&1; then
    ok "Lux installed successfully!"
    log "Quick check:"
    lux --version || lux --help | head -3
    echo
    ok "Usage: lux -u <URL> -o <file> [-c] [-r] [-t N]"
else
    err "lux is not in PATH after installation."
    exit 1
fi
