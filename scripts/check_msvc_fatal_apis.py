#!/usr/bin/env python3
"""Reject C/C++ APIs that MSVC promotes to hard errors (C4996, /W4 /WX).

Why: the windows-2022 CI leg treats deprecation warnings as hard errors,
so a `std::getenv` (or `sprintf`, `strcpy`, ...) in shared code passes the
Linux and macOS builds and fails only after a ~15-minute Windows build.
Real precedent: PR #80 needed a dedicated fix commit replacing
`std::getenv` with `getenv_s`. This hook surfaces the same class at
commit time. See CLAUDE.md "Cross-platform build strictness".

Escape hatches (all deliberate, reviewable in the diff):
- Platform-gated files never compiled by MSVC are skipped by name
  (``*linux*``, ``*posix*``, ``*x11*``, ``*wayland*``, ``*macos*``,
  ``*darwin*``, ``*.mm``).
- Lines carrying a ``NOLINT`` comment are accepted (e.g. a TU that is
  platform-gated internally via ``#ifndef _WIN32``).
- The sanctioned ``_s`` variants (``getenv_s``, ``sprintf_s``, ...) never
  match: the pattern requires ``(`` immediately after the bare name.

Pre-commit invokes this with the staged paths as positional args.
Pure-Python so the check runs at commit time on Windows without Git-Bash.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

# Bare deprecated names followed by a call paren; `[^_A-Za-z:]` blocks
# prefixed identifiers (qgetenv, vsprintf) and `::`-qualified safe calls,
# while `(?:std::)?` still catches the std-qualified forms.
BANNED = re.compile(r"(?:^|[^_A-Za-z:])(?:std::)?(getenv|sprintf|strcpy|strcat|_wgetenv)\s*\(")
PLATFORM_GATED = re.compile(r"(linux|posix|x11|wayland|macos|darwin)", re.IGNORECASE)


def check_file(path: Path) -> list[str]:
    if PLATFORM_GATED.search(path.name) or path.suffix == ".mm":
        return []
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []
    findings = []
    for lineno, line in enumerate(text.splitlines(), start=1):
        if "NOLINT" in line:
            continue
        stripped = line.lstrip()
        # Comment-only lines (// ..., * ... inside doc blocks, /* ...) may
        # legitimately NAME a banned API while documenting why the code
        # around them avoids it.
        if stripped.startswith(("//", "*", "/*")):
            continue
        match = BANNED.search(line)
        if match:
            findings.append(f"{path}:{lineno}: {match.group(1)} -> {line.strip()}")
    return findings


def main(argv: list[str]) -> int:
    all_findings: list[str] = []
    for arg in argv:
        all_findings.extend(check_file(Path(arg)))
    if not all_findings:
        return 0
    print("MSVC-fatal API use (C4996 -> hard error under /W4 /WX on windows-2022):")
    for finding in all_findings:
        print(f"  {finding}")
    print(
        "\nUse the _s variants (getenv_s / _wdupenv_s, sprintf_s or std::snprintf,\n"
        "strcpy_s, ...) or keep the TU out of the MSVC build (platform-gated file\n"
        "name / CMake condition). For a deliberate platform-guarded use add a\n"
        "// NOLINT(...) comment on the line."
    )
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
