#!/usr/bin/env bash
# Build an installable Flatpak bundle for ocapi.
#
# Requirements: flatpak and flatpak-builder. The script adds the flathub
# remote if missing and installs the required KDE Platform/Sdk runtime
# (one-time download) when it is not installed yet.
#
# Usage:
#   packaging/flatpak/build-flatpak.sh [version] [output-directory]
#
# Example:
#   packaging/flatpak/build-flatpak.sh 0.1.0 dist
#
# If no version is given, a date-based version (YYYY.MM.DD) is used.
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
VERSION="${1:-$(date -u +%Y.%m.%d)}"
OUT_DIR="${2:-$REPO_ROOT/dist}"
APP_ID="dev.sschmidhuber.ocapi"
RUNTIME_VERSION="$(sed -n 's/^runtime-version:[[:space:]]*"\?\([^"]*\)"\?/\1/p' "$MANIFEST")"

echo ">> Ensuring flathub remote"
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo

if ! flatpak info "org.kde.Sdk//${RUNTIME_VERSION}" >/dev/null 2>&1; then
    echo ">> Installing org.kde.Platform//${RUNTIME_VERSION} and org.kde.Sdk//${RUNTIME_VERSION} (one-time download)"
    flatpak install -y flathub "org.kde.Platform//${RUNTIME_VERSION}" "org.kde.Sdk//${RUNTIME_VERSION}"
fi

# flatpak-builder requires its state dir and the build dir on the same
# filesystem (e.g. /tmp is often a separate tmpfs), so keep all
# temporary directories inside the repo.
WORK_DIR="$(mktemp -d "$REPO_ROOT/.flatpak-work.XXXXXX")"
BUILD_DIR="$WORK_DIR/build"
REPO_DIR="$WORK_DIR/repo"
STATE_DIR="$WORK_DIR/state"
trap 'rm -rf "$WORK_DIR"' EXIT

mkdir -p "$OUT_DIR"
OUT_FILE="$OUT_DIR/$APP_ID-$VERSION.flatpak"

echo ">> Building $APP_ID $VERSION"
flatpak-builder --force-clean --state-dir="$STATE_DIR" --repo="$REPO_DIR" "$BUILD_DIR" "$MANIFEST"

echo ">> Creating bundle $OUT_FILE"
flatpak build-bundle "$REPO_DIR" "$OUT_FILE" "$APP_ID" --runtime="$APP_ID" 2>/dev/null \
    || flatpak build-bundle "$REPO_DIR" "$OUT_FILE" "$APP_ID"

echo ">> Done: $OUT_FILE"
echo ">> Install with: flatpak install $OUT_FILE"
