#!/usr/bin/env bash
# Reject non-ASCII codepoints in Catch2 TEST_CASE / SECTION title lines.
#
# Why: ctest passes filter args through the Win32 CMD codepage on the
# Windows CI runner. Em-dash, right-arrow, not-equal, section sign, etc.
# get mangled to '?' and Catch2's --tests-regex no longer matches, so the
# job reports "No test cases matched" and fails. This was the root cause
# of three consecutive Windows CI failures on commits 4a88cc7 / 3b1ebaa /
# 55679e2 in the 2026-05-17 session.
#
# Scope: every staged path under tests/ that ends in .cpp. We scan only
# lines containing TEST_CASE or SECTION (the only strings Catch2 uses for
# filter matching).
#
# Pre-commit invokes this with the staged paths as positional args.
set -euo pipefail

bad=0
for path in "$@"; do
    # Filter to tests/**/*.cpp - pre-commit's `files:` regex already does
    # this when invoked as a hook, but keep a defensive guard for direct
    # CLI use. Bash regex match (=~) handles the recursive pattern that
    # POSIX `case` globs cannot express portably.
    [[ "$path" =~ ^tests/.*\.cpp$ ]] || continue
    [ -f "$path" ] || continue
    # awk: emit one line per offending TEST_CASE/SECTION row with file:line:context.
    # Match only the actual Catch2 invocations (TEST_CASE( or SECTION() at the
    # start of code after optional leading whitespace), not C++ comments that
    # happen to mention "TEST_CASE" or "SECTION" in prose.
    out=$(awk '
        /^[[:space:]]*(TEST_CASE|SECTION)[[:space:]]*\(/ {
            if (match($0, /[^\x00-\x7F]/)) {
                printf("%s:%d: non-ASCII in test title: %s\n", FILENAME, NR, $0)
            }
        }
    ' "$path")
    if [ -n "$out" ]; then
        echo "$out"
        bad=1
    fi
done

if [ "$bad" -ne 0 ]; then
    cat <<'EOF'

CLAUDE.md hard rule: "Test names must be ASCII-only."
ctest passes the Catch2 filter through the Win32 CMD codepage, which
mangles Unicode chars (em-dash, right-arrow, not-equal sign, section
sign, etc.) to '?'. The filter then fails to match and the Windows CI
job reports "No test cases matched".

Replace the offending chars in the TEST_CASE / SECTION title strings
with ASCII equivalents:
   em-dash  -> '-'
   arrow    -> '->'
   not-equal-> '!='
   section  -> 'sec'
   times    -> 'x'
EOF
    exit 1
fi

exit 0
