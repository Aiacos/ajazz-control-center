#!/usr/bin/env python3
"""Reject non-ASCII codepoints in Catch2 TEST_CASE / SECTION title lines.

Why: ctest passes filter args through the Win32 CMD codepage on the Windows
CI runner. Em-dash, right-arrow, not-equal, section sign, etc. get mangled
to '?' and Catch2's --tests-regex no longer matches, so the job reports
"No test cases matched" and fails. This was the root cause of three
consecutive Windows CI failures in the 2026-05-17 session.

Scope: every staged path under tests/ that ends in .cpp. We scan only
lines containing TEST_CASE or SECTION (the only strings Catch2 uses for
filter matching).

Pre-commit invokes this with the staged paths as positional args.

Cross-platform pure-Python replacement for the original bash+awk hook so
the check runs at commit time on Windows without Git-Bash on PATH.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

# Match Catch2 invocations (TEST_CASE( / SECTION( at the start of code after
# optional leading whitespace), not comments that mention them in prose.
CATCH_LINE = re.compile(r"^\s*(?:TEST_CASE|SECTION)\s*\(")
TESTS_CPP = re.compile(r"^tests/.*\.cpp$")

GUIDANCE = """
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
"""


def main(argv: list[str]) -> int:
    bad = False
    for raw in argv:
        path = raw.replace("\\", "/")
        # Defensive guard for direct CLI use; pre-commit's `files:` already
        # narrows to tests/**/*.cpp when invoked as a hook.
        if not TESTS_CPP.match(path):
            continue
        try:
            with Path(raw).open(encoding="utf-8", errors="surrogateescape") as fh:
                lines = fh.readlines()
        except OSError:
            continue

        for lineno, line in enumerate(lines, start=1):
            if not CATCH_LINE.match(line):
                continue
            if any(ord(ch) > 0x7F for ch in line):
                stripped = line.rstrip("\n")
                print(f"{path}:{lineno}: non-ASCII in test title: {stripped}")
                bad = True

    if bad:
        print(GUIDANCE)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
