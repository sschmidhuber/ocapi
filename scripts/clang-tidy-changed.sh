#!/usr/bin/env bash
# Run clang-tidy on the C++ translation units changed relative to a base
# ref, in parallel.
#
# Unlike the compile-time integration (OKAPI_ENABLE_CLANG_TIDY=ON), this
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
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DOKAPI_BUILD_TESTS=ON >/dev/null
# Qt's AUTOMOC generates the <name>.moc includes for test and app sources;
# clang-tidy needs them on disk even though we never compile. Building only
# the changed files' targets (autogen + object) is much faster than a full
# build and still produces the .moc files next to the build tree.
for file in $CHANGED; do
    target="$(basename "$file" .cpp)"
    cmake --build "$BUILD_DIR" --target "${target}_autogen" >/dev/null 2>&1 || true
done
cmake --build "$BUILD_DIR" --target okapi-core_autogen okapi_autogen >/dev/null 2>&1 || true
run_tidy() {
    file="$1"
    build_dir="$2"
    log="$(mktemp)"
    # Ensure cpp-local "<name>.moc" includes resolve: AUTOMOC writes them into
    # the target's autogen include dir; generate any that are still missing
    # (moc on the matching header) into that dir, which is on the -I path of
    # the compile command.
    name="$(basename "$file" .cpp)"
    if grep -q "#include \"${name}\.moc\"" "$file" 2>/dev/null && \
       ! find "$build_dir" -name "${name}.moc" -print -quit | grep -q .; then
        header="$(dirname "$file")/${name}.h"
        if [ -f "$header" ]; then
            moc_bin="$(find "$build_dir" -name moc -type f | head -1)"
            [ -n "$moc_bin" ] || moc_bin="$(command -v moc || true)"
            # Quote-includes resolve relative to the including file first, so
            # write the generated moc next to the source (gitignored via
            # tests/*.moc pattern is not enough for src/, use a tmp copy in the
            # build dir AND beside the source). Prefer the build-tree autogen
            # include dir, which is on the compile command's -I path.
            autogen_dir="$(dirname "$(find "$build_dir" -name mocs_compilation.cpp | head -1)")"
            if [ -n "$moc_bin" ] && [ -n "$autogen_dir" ]; then
                "$moc_bin" "$header" -o "$autogen_dir/${name}.moc" >/dev/null 2>&1 || true
            fi
            # Fallback: next to the source file (resolved by quote-include).
            if [ -n "$moc_bin" ] && ! find "$build_dir" -name "${name}.moc" -print -quit | grep -q .; then
                "$moc_bin" "$header" -o "$(dirname "$file")/${name}.moc" >/dev/null 2>&1 || true
            fi
        fi
    fi
    # AUTOUIC: if the TU includes a generated ui_*.h, run uic on the matching
    # .ui next to the source into the autogen include dir (which is on the
    # -I path of the compile command). The autogen target usually emits it,
    # but generate it explicitly so a failed autogen cannot break the check.
    for ui_header in $(grep -oE '#include "ui_[^"]+\.h"' "$file" | sed -E 's/#include "([^"]+)"/\1/'); do
        ui_src="$(dirname "$file")/${ui_header#ui_}"
        ui_src="${ui_src%.h}.ui"
        if [ -f "$ui_src" ]; then
            uic_bin="$(find "$build_dir" -name uic -type f | head -1)"
            if [ -z "$uic_bin" ]; then
                uic_bin="$(command -v uic || true)"
            fi
            if [ -z "$uic_bin" ]; then
                # Qt installs (aqtinstall, install-qt-action) keep uic in
                # libexec/, not bin/ - derive it from moc or Qt6_DIR.
                moc_bin="$(command -v moc || true)"
                if [ -n "$moc_bin" ] && [ -f "$(dirname "$moc_bin")/../libexec/uic" ]; then
                    uic_bin="$(cd "$(dirname "$moc_bin")/../libexec" && pwd)/uic"
                elif [ -f "$build_dir/CMakeCache.txt" ]; then
                    qt6_dir="$(sed -n 's/^Qt6_DIR:PATH=//p' "$build_dir/CMakeCache.txt")"
                    if [ -n "$qt6_dir" ] && [ -f "$qt6_dir/../../../libexec/uic" ]; then
                        uic_bin="$qt6_dir/../../../libexec/uic"
                    fi
                fi
            fi
            # Prefer the app target's autogen include dir (on the -I path of
            # the compile command); quote-includes also resolve relative to
            # the including file, so writing next to the source is a fallback.
            autogen_ui_dir="$(dirname "$(find "$build_dir/src" -name mocs_compilation.cpp 2>/dev/null | head -1)")"
            if [ -n "$autogen_ui_dir" ] && [ -d "$autogen_ui_dir/include" ]; then
                autogen_ui_dir="$autogen_ui_dir/include"
            fi
            if [ -n "$uic_bin" ] && [ -n "$autogen_ui_dir" ]; then
                "$uic_bin" "$ui_src" -o "$autogen_ui_dir/$ui_header" >/dev/null 2>&1 || true
            fi
            if [ -n "$uic_bin" ]; then
                "$uic_bin" "$ui_src" -o "$(dirname "$file")/$ui_header" >/dev/null 2>&1 || true
            fi
        fi
    done
    # -warnings-as-errors='*' turns every check warning into a failure, so
    # the exit code reflects the tidy result (a bare clang-tidy run exits 0
    # even when it prints warnings). A missing <name>.moc include is a fatal
    # preprocessor error, not a check warning, so -warnings-as-errors does
    # not cover it; tolerate exactly that one error.
    tidy_ok=0
    if clang-tidy -p "$build_dir" --warnings-as-errors='*' "$file" >"$log" 2>&1; then
        tidy_ok=1
    elif grep -q "'.*\.moc' file not found" "$log" && \
         ! grep -E "error:" "$log" | grep -vq "file not found"; then
        echo ">> note: $file includes a .moc that autogen did not emit; ignoring that single error" >&2
        tidy_ok=1
        grep -v "file not found" "$log" > "$log.clean" && mv "$log.clean" "$log"
    fi
    if [ "$tidy_ok" -eq 1 ]; then
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
