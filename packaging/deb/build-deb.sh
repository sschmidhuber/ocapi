#!/usr/bin/env bash
# Build an installable DEB package for ocapi.
#
# Requirements (Debian / Ubuntu):
#   sudo apt-get install build-essential cmake qt6-base-dev qt6-base-dev-tools \
#     libqt6sql6-sqlite libgl-dev dpkg-dev
#
# Usage:
#   packaging/deb/build-deb.sh [version] [output-directory]
#
# Example:
#   packaging/deb/build-deb.sh 0.1.0 dist
#
# Produces: <output-directory>/ocapi_<version>-1_amd64.deb
#
# The DEB is unsigned (no dpkg-sig / debsign step). Signing a .deb for
# repository distribution requires a GPG key; a plain unsigned .deb
# installs fine with:  sudo apt install ./ocapi_<version>-1_amd64.deb
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VERSION="${1:-$(git -C "$REPO_ROOT" describe --tags --abbrev=0 2>/dev/null || echo 0.1.0)}"
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

cat > "$DEBIAN/control" <<EOF
Package: ocapi
Version: $VERSION-1
Section: net
Priority: optional
Architecture: $ARCH
Maintainer: ocapi <ocapi@sschmidhuber.dev>
Depends: libc6 (>= 2.34), libstdc++6 (>= 13), libgcc-s1,
 qt6-base (>= 6.5), libqt6sql6-sqlite, libgl1, libegl1, libxkbcommon0,
 libfontconfig1, libdbus-1-3, libglib2.0-0
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
