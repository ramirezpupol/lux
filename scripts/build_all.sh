#!/usr/bin/env bash
#
# build_all.sh — build every configuration available on this host.
#
# Cross-platform builds (Windows/macOS/ARM) are done natively by CI
# (.github/workflows/release.yml) on tagged releases. On a local host this
# script builds what the host supports:
#
#   1. Native build (build/)
#   2. Static-libstdc++ build (build-static/) — portable binary
#   3. Termux .deb (if dpkg-deb is available)
#
# Usage:
#   ./scripts/build_all.sh            # everything the host supports
#   ./scripts/build_all.sh --deb      # only the termux deb

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

VERSION="$(grep -m1 'project(Lux VERSION' CMakeLists.txt | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')"
ARCH="$(uname -m)"

log()  { printf '\033[1;34m[*]\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m[✓]\033[0m %s\n' "$*"; }

build_native() {
    log "Building native (build/)"
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)"
    ok "build/lux"
}

build_static() {
    log "Building with static libstdc++/libgcc (build-static/)"
    cmake -S . -B build-static -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
    cmake --build build-static --config Release -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)"
    ok "build-static/lux (portable where libcurl exists)"
}

build_termux_deb() {
    # Builds a .deb for the HOST architecture using the Termux prefix layout.
    # Real Termux packages are produced by CI (termux-docker container).
    command -v dpkg-deb > /dev/null || { log "dpkg-deb not available; skipping deb"; return 0; }

    log "Building Termux-layout .deb for ${ARCH}"
    cmake -S . -B build-deb -DCMAKE_BUILD_TYPE=Release
    cmake --build build-deb --config Release -j"$(nproc)"

    local PKGROOT="pkg/lux"
    rm -rf pkg
    mkdir -p "$PKGROOT/DEBIAN" \
             "$PKGROOT/data/data/com.termux/files/usr/bin" \
             "$PKGROOT/data/data/com.termux/files/usr/share/doc/lux"

    cp build-deb/lux "$PKGROOT/data/data/com.termux/files/usr/bin/lux"
    cp README.md "$PKGROOT/data/data/com.termux/files/usr/share/doc/lux/" 2>/dev/null || true

    cat > "$PKGROOT/DEBIAN/control" <<EOF
Package: lux
Version: ${VERSION}
Section: net
Priority: optional
Architecture: ${ARCH}
Maintainer: ramirezpupol
Depends: libcurl
Description: Robust CLI file downloader
 Multi-threaded ranged downloads, HTTP Range resume and recursive
 folder downloads. Guarantees the file gets downloaded.
EOF

    dpkg-deb --build --root-owner-group "$PKGROOT" "lux-termux-${ARCH}.deb"
    ok "lux-termux-${ARCH}.deb"
    rm -rf pkg
}

case "${1:-all}" in
    --deb)  build_termux_deb ;;
    all)
        build_native
        build_static
        build_termux_deb
        ;;
    *) echo "usage: $0 [--deb]"; exit 1 ;;
esac

log "Done. Cross-platform binaries are built by CI on tagged releases:"
log "  git tag v${VERSION} && git push origin v${VERSION}"
