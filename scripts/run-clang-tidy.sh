#!/usr/bin/env bash
# Run clang-tidy against the changed files using the compile_commands.json from
# the `dev` CMake preset. Idempotent and safe to run from pre-commit (manual
# stage) or `make tidy`.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/dev"
COMPDB="${BUILD_DIR}/compile_commands.json"

if [[ ! -f $COMPDB ]]; then
    echo "[clang-tidy] No compile_commands.json at ${COMPDB}." >&2
    echo "[clang-tidy] Configure first:  cmake --preset dev" >&2
    exit 0 # don't fail pre-commit if the user hasn't configured yet
fi

# Filter to project sources only — clang-tidy on hidapi / pybind11 is noisy.
files=()
for f in "$@"; do
    case "$f" in
        src/* | tests/*) files+=("$f") ;;
    esac
done

if [[ ${#files[@]} -eq 0 ]]; then
    exit 0
fi

clang-tidy --quiet -p "$BUILD_DIR" "${files[@]}"
