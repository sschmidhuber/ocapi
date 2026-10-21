#!/usr/bin/env bash
# Build an installable Flatpak bundle for ocapi.
#
# Requirements: flatpak, flatpak-builder, and the freedesktop git remote:
#   flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
#
# Usage:
#   packaging/flatpak/build-flatpak.sh [version] [output-directory]
#
# Example:
#   packaging/flatpak/build-flatpak.sh 0.1.0 dist
#
# Produces: <output-directory>/dev.sschmidhuber.ocapi-<version>.flatpak
#
# The resulting .flatpak is a single-file bundle that can be installed
# with:  flatpak install dev.sschmidhuber.ocapi-<version>.flatpak
# Distribution of the bundle is unsigned by default; signing a Flatpak
# bundle for a repository requires a GPG key registered with the remote,
# which is out of scope for a self-built bundle.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MANIFEST="$REPO_ROOT/packaging/flatpak/dev.sschmidhuber.ocapi.yml"
VERSION="${1:-$(git -C "$REPO_ROOT" describe --tags --abbrev=0 2>/dev/null || echo 0.1.0)}"
OUT_DIR="${2:-$REPO_ROOT/dist}"
APP_ID="dev.sschmidhuber.ocapi"

BUILD_DIR="$(mktemp -d /tmp/ocapi-flatpak-build.XXXXXX)"
REPO_DIR="$(mktemp -d /tmp/ocapi-flatpak-repo.XXXXXX)"
trap 'rm -rf "$BUILD_DIR" "$REPO_DIR"' EXIT

mkdir -p "$OUT_DIR"
OUT_FILE="$OUT_DIR/$APP_ID-$VERSION.flatpak"

echo ">> Building $APP_ID $VERSION"
flatpak-builder --force-clean --repo="$REPO_DIR" "$BUILD_DIR" "$MANIFEST"

echo ">> Creating bundle $OUT_FILE"
flatpak build-bundle "$REPO_DIR" "$OUT_FILE" "$APP_ID" --runtime="$APP_ID" 2>/dev/null \
    || flatpak build-bundle "$REPO_DIR" "$OUT_FILE" "$APP_ID"

echo ">> Done: $OUT_FILE"
echo ">> Install with: flatpak install $OUT_FILE"
