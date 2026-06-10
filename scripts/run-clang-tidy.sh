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
# Headers (.hpp) and platform-gated TUs (*_win32.cpp on Linux) have no entry
# in the compdb; clang-tidy hard-errors on them, so keep only files the
# compdb actually knows how to compile.
files=()
for f in "$@"; do
    case "$f" in
        src/* | tests/*)
            if grep -qF "\"$(basename "$f")\"" "$COMPDB" || grep -qF "/$f" "$COMPDB"; then
                files+=("$f")
            fi
            ;;
    esac
done

if [[ ${#files[@]} -eq 0 ]]; then
    exit 0
fi

# GCC >= 15 writes C++20 module-scanning flags (-fmodules-ts,
# -fmodule-mapper=..., -fdeps-format=p1689r5, -fdeps-file=..., -fdeps-target=...)
# into compile_commands.json; clang-tidy's clang driver rejects them as
# "unknown argument" hard errors. Strip them into a filtered copy of the
# compdb so the hook works regardless of the configuring compiler.
TIDY_DB_DIR="$(mktemp -d)"
trap 'rm -rf "$TIDY_DB_DIR"' EXIT
sed -E 's/-f(deps-format|deps-file|deps-target|module-mapper)=[^" ]*//g; s/-fmodules-ts//g' \
    "$COMPDB" >"${TIDY_DB_DIR}/compile_commands.json"

clang-tidy --quiet -p "$TIDY_DB_DIR" "${files[@]}"
