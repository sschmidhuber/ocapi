#!/usr/bin/env bash
# Print the C++ source files (.cpp/.cc/.cxx) changed relative to a base ref.
#
# Used by the CI clang-tidy job to check only what a change actually
# touched instead of the whole codebase.
#
# Usage:
#   scripts/ci-changed-files.sh [base-ref]
#
# The base ref defaults to the merge base with origin/main, which is what
# a pull request changed. On a push to main, origin/main~1 is used as a
# fallback so the script still works there.
#
# Environment:
#   GITHUB_BASE_REF  (optional) branch the PR targets; used to compute the
#                    merge base when set by GitHub Actions.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BASE_REF="${1:-}"

if [ -z "$BASE_REF" ]; then
    if [ -n "${GITHUB_BASE_REF:-}" ]; then
        TARGET_BRANCH="origin/${GITHUB_BASE_REF}"
    elif git show-ref --verify --quiet refs/remotes/origin/main; then
        TARGET_BRANCH="origin/main"
    else
        TARGET_BRANCH="main"
    fi
    if git merge-base --is-ancestor "$TARGET_BRANCH" HEAD 2>/dev/null; then
        # Push to the target branch itself: compare against the previous
        # commit so the last commit is checked.
        BASE_REF="HEAD~1"
    else
        BASE_REF="$(git merge-base HEAD "$TARGET_BRANCH")"
    fi
fi

git diff --name-only --diff-filter=ACMR "$BASE_REF" -- '*.cpp' '*.cc' '*.cxx' \
    | grep -E '\.(cpp|cc|cxx)$' || true
