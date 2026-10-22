#!/usr/bin/env bash
# Build an installable DEB package for ocapi.
#
# Requirements (Debian / Ubuntu):
#   sudo apt-get install build-essential cmake ninja-build qt6-base-dev \
#     qt6-base-dev-tools qt6-multimedia-dev qt6-websockets-dev \
#     libqt6sql6-sqlite libgl-dev dpkg-dev
#
# Usage:
#   packaging/deb/build-deb.sh [version] [output-directory]
#
# Example:
#   packaging/deb/build-deb.sh 0.1.0 dist
#
# If no version is given, a date-based version (YYYY.MM.DD) is used.
#
# Produces: <output-directory>/ocapi_<version>-1_amd64.deb
#
# The DEB is unsigned (no dpkg-sig / debsign step). Signing a .deb for
# repository distribution requires a GPG key; a plain unsigned .deb
# installs fine with:  sudo apt install ./ocapi_<version>-1_amd64.deb
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VERSION="${1:-$(date -u +%Y.%m.%d)}"
OUT_DIR="${2:-$REPO_ROOT/dist}"
ARCH="$(dpkg --print-architecture)"
PKG_DIR="$(mktemp -d /tmp/ocapi-deb.XXXXXX)"
trap 'rm -rf "$PKG_DIR"' EXIT

STAGE="$PKG_DIR/ocapi-$VERSION"
DEBIAN="$STAGE/DEBIAN"
mkdir -p "$OUT_DIR" "$DEBIAN"

echo ">> Building ocapi $VERSION (Release)"
CMAKE_ARGS=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DOCAPI_BUILD_TESTS=OFF)
if [ -n "${CMAKE_PREFIX_PATH:-}" ]; then
    CMAKE_ARGS+=(-DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH")
fi
if [ -n "${CMAKE_LIBRARY_PATH:-}" ]; then
    CMAKE_ARGS+=(-DCMAKE_LIBRARY_PATH="$CMAKE_LIBRARY_PATH")
fi
if [ -n "${CMAKE_INCLUDE_PATH:-}" ]; then
    CMAKE_ARGS+=(-DCMAKE_INCLUDE_PATH="$CMAKE_INCLUDE_PATH")
fi
cmake -B "$PKG_DIR/build" -S "$REPO_ROOT" "${CMAKE_ARGS[@]}"
cmake --build "$PKG_DIR/build"

echo ">> Staging install tree"
DESTDIR="$STAGE" cmake --install "$PKG_DIR/build" --prefix /usr

# Resolve runtime dependencies from the staged binaries with dpkg-shlibdeps
# so the package names match the Debian/Ubuntu release the build runs on
# (e.g. libqt6core6t64 on trixie/noble). qt6-base is not a Debian package.
resolve_deps_shlibdeps() {
    local out
    if ! out="$(dpkg-shlibdeps -O --ignore-missing-info "$STAGE/usr/bin/ocapi" 2>&1)"; then
        echo "!! dpkg-shlibdeps failed, falling back to ldd-based dependency scan:" >&2
        printf '%s\n' "$out" >&2
        return 1
    fi
    printf '%s\n' "$out" | sed -n 's/^shlibs:Depends=//p' | sed 's/,$//'
}

resolve_deps_ldd() {
    local lib pkg seen="\n" deps=""
    while IFS= read -r lib; do
        pkg="$(dpkg -S "$lib" 2>/dev/null | sed 's/: .*//; q' || true)"
        [ -n "$pkg" ] || continue
        case "$seen" in
            *"\n$pkg\n"*) continue ;;
        esac
        seen="$seen\n$pkg\n"
        deps="${deps:+$deps, }$pkg"
    done < <(ldd "$STAGE/usr/bin/ocapi" 2>/dev/null | sed -n 's/.*=> \(\/.*\) (.*/\1/p')
    printf '%s\n' "$deps"
}

DEPS=""
if command -v dpkg-shlibdeps >/dev/null 2>&1; then
    DEPS="$(resolve_deps_shlibdeps || true)"
fi
if [ -z "$DEPS" ]; then
    echo ">> Resolving dependencies via ldd + dpkg -S" >&2
    DEPS="$(resolve_deps_ldd)"
fi
if [ -z "$DEPS" ]; then
    echo "!! Could not resolve runtime dependencies, using hardcoded list" >&2
    DEPS="libc6 (>= 2.34), libstdc++6 (>= 13), libgcc-s1,
 libqt6core6t64 (>= 6.5), libqt6gui6t64 (>= 6.5), libqt6widgets6t64 (>= 6.5),
 libqt6network6t64 (>= 6.5), libqt6sql6t64 (>= 6.5),
 libqt6multimedia6t64 (>= 6.5), libqt6websockets6t64 (>= 6.5),
 libgl1, libegl1, libxkbcommon0, libfontconfig1,
 libdbus-1-3, libglib2.0-0t64"
fi
# The SQLite Qt SQL plugin is loaded at runtime, not linked.
case " $DEPS " in
    *" libqt6sql6-sqlite "*) ;;
    *) DEPS="$DEPS, libqt6sql6-sqlite" ;;
esac

cat > "$DEBIAN/control" <<EOF
Package: ocapi
Version: $VERSION-1
Section: net
Priority: optional
Architecture: $ARCH
Maintainer: ocapi <ocapi@sschmidhuber.dev>
Depends: $DEPS
Homepage: https://github.com/sschmidhuber/ocapi
Description: A desktop chat client for local and cloud LLM inference backends
 ocapi talks to local inference servers (Ollama, llama.cpp, vLLM) and cloud
 APIs (OpenAI, Anthropic, Google Gemini, Mistral AI, and any
 OpenAI-compatible endpoint), locally or on remote machines. It streams
 responses over Server-Sent Events, supports file attachments and integrates
 MCP (Model Context Protocol) servers.
EOF

cp "$REPO_ROOT/LICENSE" "$DEBIAN/copyright"

echo ">> Building DEB"
DEB_FILE="$OUT_DIR/ocapi_${VERSION}-1_${ARCH}.deb"
if command -v fakeroot >/dev/null 2>&1; then
    fakeroot dpkg-deb --root-owner-group --build "$STAGE" "$DEB_FILE"
else
    dpkg-deb --root-owner-group --build "$STAGE" "$DEB_FILE"
fi

echo ">> Done: $DEB_FILE"
echo ">> Install with: sudo apt install $DEB_FILE"
