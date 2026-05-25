#!/usr/bin/env python3
"""Reject raw USB capture files at commit time (CAPTURE-01 / Pitfall 17).

Pre-commit invokes this with the staged paths as positional args (we set
``pass_filenames: true``). For each staged path we check:

  1. Extension blocklist: basename matches ``*.pcap`` or ``*.pcapng``
     (case-insensitive) -> REJECT outright.
  2. Captures-sink size guardrail: path is under
     ``.planning/research/captures/``, is NOT the README or .gitignore, IS
     binary, AND exceeds 10240 bytes -> REJECT.

Rejection message points at docs/policies/capture-data-hygiene.md and
scripts/hex-to-cpparray.py per CAPTURE-01 spec.

Cross-platform pure-Python replacement for the original bash hook so the
check runs at commit time on Windows without Git-Bash on PATH. Output is
ASCII-only (no em-dash, no arrows) so Windows CMD codepages render the
rejection cleanly (CLAUDE.md hard rule on test/string ASCII-only).
"""

from __future__ import annotations

import sys
from pathlib import Path

SINK_DIR = ".planning/research/captures"
SINK_README = f"{SINK_DIR}/README.md"
SINK_GITIGNORE = f"{SINK_DIR}/.gitignore"
SIZE_LIMIT_BYTES = 10240

POLICY_DOC = "docs/policies/capture-data-hygiene.md"
SANITISER_SCRIPT = "scripts/hex-to-cpparray.py"


def emit_reject(path: str, reason: str) -> None:
    print(f"REJECTED: {path}", file=sys.stderr)
    print(f"  Reason: {reason}", file=sys.stderr)
    print(
        f"  Remediation: See {POLICY_DOC} and use {SANITISER_SCRIPT} to produce",
        file=sys.stderr,
    )
    print(
        "  a sanitised fixture under tests/integration/fixtures/<codename>/.",
        file=sys.stderr,
    )


def is_binary(path: str) -> bool:
    """Best-effort binary sniff: a NUL byte in the first 8 KiB, like git.

    Missing / unreadable files fall through as "not binary" so we never
    false-reject on weird FS state.
    """
    try:
        with Path(path).open("rb") as fh:
            chunk = fh.read(8192)
    except OSError:
        return False
    return b"\x00" in chunk


def main(argv: list[str]) -> int:
    bad = 0
    for raw in argv:
        # Normalise leading ./ and backslashes so matching is uniform across
        # platforms; pre-commit passes forward-slash paths but be defensive.
        path = raw.replace("\\", "/")
        normalised = path[2:] if path.startswith("./") else path
        fs_path = Path(normalised)

        # Skip non-files (deletions show up as paths no longer on disk).
        if not fs_path.is_file():
            continue

        # Rule 1: extension blocklist (case-insensitive).
        if fs_path.suffix.lower() in (".pcap", ".pcapng"):
            emit_reject(
                path,
                "raw USB capture file (extension blocklist: "
                "*.pcap / *.pcapng, case-insensitive)",
            )
            bad = 1
            continue

        # Rule 2: captures-sink size guardrail.
        if normalised == SINK_DIR or normalised.startswith(SINK_DIR + "/"):
            if normalised in (SINK_README, SINK_GITIGNORE):
                continue
            try:
                size_bytes = fs_path.stat().st_size
            except OSError:
                continue
            if size_bytes > SIZE_LIMIT_BYTES and is_binary(normalised):
                emit_reject(
                    path,
                    f"binary file >{SIZE_LIMIT_BYTES} bytes under "
                    f"{SINK_DIR}/ (captures-sink size guardrail)",
                )
                bad = 1

    return bad


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
