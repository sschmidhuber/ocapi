#!/usr/bin/env bash
# Build an installable RPM package for ocapi.
#
# Requirements (Fedora / RHEL / openSUSE):
#   sudo dnf install rpm-build cmake ninja-build gcc-c++ qt6-qtbase-devel
#
# Usage:
#   packaging/rpm/build-rpm.sh [version] [output-directory]
#
# Example:
#   packaging/rpm/build-rpm.sh 0.1.0 dist
#
# Produces: <output-directory>/ocapi-<version>-1.<arch>.rpm
#
# The RPM is unsigned. Signing requires a GPG key configured in ~/.rpmmacros
# (%_gpg_name); with a key present, add "--sign" to the rpmbuild call in
# this script or run:  rpm --addsign <package>.rpm
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VERSION="${1:-$(git -C "$REPO_ROOT" describe --tags --abbrev=0 2>/dev/null || echo 0.1.0)}"
OUT_DIR="${2:-$REPO_ROOT/dist}"
TOPDIR="$(mktemp -d /tmp/ocapi-rpm.XXXXXX)"
trap 'rm -rf "$TOPDIR"' EXIT

mkdir -p "$OUT_DIR" "$TOPDIR"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

echo ">> Creating source tarball ocapi-$VERSION.tar.gz"
TARBALL="$TOPDIR/SOURCES/ocapi-$VERSION.tar.gz"
# Package the working tree (not HEAD) so local changes are included.
tar -czf "$TARBALL" -C "$REPO_ROOT" \
    --transform "s,^\\.,ocapi-$VERSION," \
    --exclude=.git --exclude=build --exclude=dist .

echo ">> Building RPM ocapi-$VERSION"
rpmbuild -bb "$REPO_ROOT/packaging/rpm/ocapi.spec" \
    --define "_topdir $TOPDIR" \
    --define "version $VERSION"

RPM_FILE="$(find "$TOPDIR/RPMS" -name 'ocapi-*.rpm' | head -n 1)"
cp "$RPM_FILE" "$OUT_DIR/"

echo ">> Done: $OUT_DIR/$(basename "$RPM_FILE")"
echo ">> Install with: sudo dnf install $OUT_DIR/$(basename "$RPM_FILE")"
