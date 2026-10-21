#!/usr/bin/env bash
# Run clang-tidy on the C++ translation units changed relative to a base
# ref, in parallel.
#
# Unlike the compile-time integration (OCAPI_ENABLE_CLANG_TIDY=ON), this
# script does not rebuild the project first: it configures a build tree
# purely to generate compile_commands.json, then invokes clang-tidy
# directly on the changed .cpp files. Files are checked concurrently with
# xargs -P, so the step scales with the number of runner cores instead of
# checking the whole codebase serially.
#
# Usage:
#   scripts/clang-tidy-changed.sh [base-ref] [build-dir]
#
# Environment:
#   JOBS   number of parallel clang-tidy processes (default: nproc)
#
# Exit status: non-zero if clang-tidy reports any warning or error on a
# changed file. Note: a bare clang-tidy exit code only reflects whether the
# file compiled; check warnings are captured via -warnings-as-errors='*'.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BASE_REF="${1:-}"
BUILD_DIR="${2:-build-tidy}"
JOBS="${JOBS:-$(nproc)}"

if ! command -v clang-tidy >/dev/null 2>&1; then
    echo "error: clang-tidy not found in PATH" >&2
    exit 1
fi

if [ -z "$BASE_REF" ]; then
    CHANGED="$(scripts/ci-changed-files.sh)"
else
    CHANGED="$(scripts/ci-changed-files.sh "$BASE_REF")"
fi

if [ -z "$CHANGED" ]; then
    echo ">> No changed C++ source files; nothing to check."
    exit 0
fi

echo ">> Changed translation units:"
printf '   %s\n' $CHANGED

echo ">> Generating compile_commands.json (autogen + configure only, no compile)"
cmake -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DOCAPI_BUILD_TESTS=ON >/dev/null
# Qt's AUTOMOC generates the <name>.moc includes for test and app sources;
# clang-tidy needs them on disk even though we never compile. Building only
# the changed files' targets (autogen + object) is much faster than a full
# build and still produces the .moc files next to the build tree.
for file in $CHANGED; do
    target="$(basename "$file" .cpp)"
    cmake --build "$BUILD_DIR" --target "${target}_autogen" >/dev/null 2>&1 || true
done
cmake --build "$BUILD_DIR" --target ocapi-core_autogen ocapi_autogen >/dev/null 2>&1 || true

run_tidy() {
    file="$1"
    build_dir="$2"
    log="$(mktemp)"
    # -warnings-as-errors='*' turns every check warning into a failure, so
    # the exit code reflects the tidy result (a bare clang-tidy run exits 0
    # even when it prints warnings).
    if clang-tidy -p "$build_dir" --warnings-as-errors='*' "$file" >"$log" 2>&1; then
        cat "$log"
        echo ">> OK: $file"
    else
        cat "$log" >&2
        echo ">> FAILED: $file" >&2
        rm -f "$log"
        return 1
    fi
    rm -f "$log"
}
export -f run_tidy

echo ">> Running clang-tidy on changed files ($JOBS parallel jobs)"
FAILED=0
printf '%s\n' $CHANGED | xargs -P "$JOBS" -I{} bash -c 'run_tidy "$@"' _ {} "$BUILD_DIR" \
    || FAILED=1

if [ "$FAILED" -ne 0 ]; then
    echo "clang-tidy reported issues on changed files" >&2
    exit 1
fi
echo ">> clang-tidy: all changed files clean"
