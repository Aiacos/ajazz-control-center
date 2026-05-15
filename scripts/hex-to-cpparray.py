#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Convert a usbrply JSON capture into a C++ header with std::array literals.

Pipeline shape (see docs/protocols/CAPTURING.md):

    tshark -i usbmonN -w cap.pcap        # raw capture, NEVER committed
    usbrply -j cap.pcap > cap.json       # JSON intermediate
    scripts/hex-to-cpparray.py cap.json --device <codename> --capture <label> \
        > tests/integration/fixtures/<codename>/<label>.h

The emitted header contains a single ``inline constexpr
std::array<std::uint8_t, N>`` literal inside namespace
``ajazz::tests::fixtures``, matching STACK section "Test-replay
infrastructure" and the existing precedent in
``tests/integration/fixtures/akp153/*.hex``.

Anti-features (load-bearing):
- NO third-party imports (Python stdlib only). The dev-time CI environment
  does not require ``pipx install usbrply``.
- NO libpcap, no scapy, no pcapng -- the script consumes the JSON shape
  ``usbrply -j`` produces, not the raw ``.pcap`` bytes (D-03).
- NO C++ JSON library in any runtime path (COD-031 boundary; the
  agent-side JSON library stays PRIVATE-linked to ajazz_plugins).
  Runtime tests ``#include`` the produced header directly.
- ASCII-only emitted header (Pitfall 32: Win32 CMD codepage mangles
  non-ASCII characters in test names that flow through ctest filters).
"""

from __future__ import annotations

import argparse
import datetime as _dt
import json
import re
import sys
from pathlib import Path

# --------------------------------------------------------------------------
# Constants
# --------------------------------------------------------------------------
MAX_BYTES_PER_LINE = 8
HEX_RE = re.compile(r"^[0-9a-fA-F]+$")
DEVICE_CODENAME_RE = re.compile(r"^[a-z0-9_]+$")
LABEL_RE = re.compile(r"^[A-Za-z0-9_\-]+$")
ALLOWED_TYPES = (
    "controlWrite",
    "controlRead",
    "interruptIn",
    "interruptOut",
    "any",
)


# --------------------------------------------------------------------------
# Argument parsing
# --------------------------------------------------------------------------
def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="hex-to-cpparray.py",
        description=(
            "Convert a usbrply JSON capture to a C++ header containing an "
            "inline constexpr std::array<std::uint8_t, N> literal."
        ),
    )
    parser.add_argument(
        "input",
        help="Path to a usbrply -j JSON file, or '-' to read JSON from stdin.",
    )
    parser.add_argument(
        "--device",
        required=True,
        help=(
            "Device codename (ASCII snake_case, e.g. 'akp03_variant_3004'). "
            "Becomes the leading half of the C++ identifier."
        ),
    )
    parser.add_argument(
        "--capture",
        required=True,
        help=(
            "Human-readable capture label (e.g. 'image-upload-first-chunk'). "
            "Normalised to UPPER_SNAKE_CASE for the C++ identifier."
        ),
    )
    parser.add_argument(
        "--packet-index",
        type=int,
        default=0,
        help="Which data-bearing packet from the JSON to convert (default: 0).",
    )
    parser.add_argument(
        "--type",
        dest="type_filter",
        default="any",
        choices=ALLOWED_TYPES,
        help="Filter to one usbrply packet type (default: any).",
    )
    return parser.parse_args(argv)


# --------------------------------------------------------------------------
# Identifier helpers
# --------------------------------------------------------------------------
def normalise_label(s: str) -> str:
    """Normalise a free-form label to UPPER_SNAKE_CASE."""
    if not LABEL_RE.match(s):
        msg = (
            f"--capture label must be ASCII alnum/hyphen/underscore only, got: {s!r} "
            f"(Pitfall 32: Win32 CMD codepage mangles non-ASCII in ctest filters)"
        )
        raise ValueError(msg)
    return s.replace("-", "_").upper()


def normalise_device(s: str) -> str:
    """Normalise a device codename to UPPER_SNAKE_CASE.

    The codename is required to be ASCII snake_case on input (lowercase
    letters, digits, underscores). This rejects shell-special characters
    upfront per the threat register T-09-13.
    """
    if not DEVICE_CODENAME_RE.match(s):
        msg = (
            f"--device codename must match [a-z0-9_]+ (ASCII snake_case), got: {s!r} "
            f"(Pitfall 32 + T-09-13 shell-injection guard)"
        )
        raise ValueError(msg)
    return s.upper()


# --------------------------------------------------------------------------
# usbrply JSON -> bytes
# --------------------------------------------------------------------------
def select_packet(usbrply_doc: dict, index: int, type_filter: str) -> bytes:
    """Pick one packet's ``data`` field and return it as raw bytes.

    Args:
        usbrply_doc: Parsed ``usbrply -j`` document, expected to have a
            top-level ``packs`` list of dicts each with a ``data`` hex string.
        index: Zero-based index into the filtered packet list.
        type_filter: One of ``ALLOWED_TYPES``; ``"any"`` accepts every
            packet that has a ``data`` field.

    Raises:
        ValueError: On malformed input, empty filter result, or out-of-range
            index. The error message is intended to be surfaced to stderr.
    """
    packs = usbrply_doc.get("packs")
    if not isinstance(packs, list):
        msg = "usbrply JSON missing top-level 'packs' list"
        raise ValueError(msg)
    if not packs:
        msg = "usbrply JSON contains no packets (empty 'packs' list)"
        raise ValueError(msg)

    candidates: list[dict] = []
    for pkt in packs:
        if not isinstance(pkt, dict):
            continue
        if "data" not in pkt:
            continue
        if type_filter != "any" and pkt.get("type") != type_filter:
            continue
        candidates.append(pkt)

    if not candidates:
        msg = (
            f"no packets matched filter type={type_filter!r} "
            f"(out of {len(packs)} total packet entries)"
        )
        raise ValueError(msg)

    if index < 0 or index >= len(candidates):
        msg = (
            f"--packet-index {index} out of range; "
            f"{len(candidates)} packet(s) matched type={type_filter!r}"
        )
        raise ValueError(msg)

    pkt = candidates[index]
    data = pkt.get("data")
    if not isinstance(data, str) or not data:
        msg = f"selected packet has empty or non-string 'data' field: {pkt!r}"
        raise ValueError(msg)
    if not HEX_RE.match(data):
        msg = f"selected packet 'data' field is not pure hex: {data!r} (T-09-11 tampering guard)"
        raise ValueError(msg)
    if len(data) % 2 != 0:
        msg = f"selected packet 'data' field has odd hex length: {len(data)}"
        raise ValueError(msg)

    return bytes.fromhex(data)


# --------------------------------------------------------------------------
# Header rendering
# --------------------------------------------------------------------------
def _now_utc_iso() -> str:
    """UTC timestamp in ISO 8601, second precision (no microseconds)."""
    now = _dt.datetime.now(tz=_dt.UTC).replace(microsecond=0)
    # isoformat() produces '+00:00'; we want trailing 'Z' for parity with
    # the example in the plan's <interfaces> block.
    return now.isoformat().replace("+00:00", "Z")


def emit_header(
    device_upper: str,
    label_upper: str,
    payload: bytes,
    device_raw: str,
    capture_raw: str,
) -> str:
    """Render the full C++ header file content as a single string.

    Identifier shape: ``<DEVICE>_<LABEL>_BYTES``.
    Output ends with a single trailing newline so the
    ``end-of-file-fixer`` pre-commit hook is a no-op on the emitted file.
    """
    identifier = f"{device_upper}_{label_upper}_BYTES"
    n = len(payload)
    timestamp = _now_utc_iso()

    lines: list[str] = [
        "// SPDX-License-Identifier: GPL-3.0-or-later",
        "// AUTOGENERATED by scripts/hex-to-cpparray.py -- do not edit by hand.",
        f"// Source: usbrply JSON capture, device={device_raw}, label={capture_raw}.",
        f"// Generated: {timestamp} (UTC, timestamp at script invocation time).",
        "#pragma once",
        "#include <array>",
        "#include <cstdint>",
        "",
        "namespace ajazz::tests::fixtures {",
        "",
        f"inline constexpr std::array<std::uint8_t, {n}> {identifier} = {{",
    ]

    # Body: hex bytes, 8 per source line, 4-space indent, lower-case 0xNN.
    for chunk_start in range(0, n, MAX_BYTES_PER_LINE):
        chunk = payload[chunk_start : chunk_start + MAX_BYTES_PER_LINE]
        rendered = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append(f"    {rendered},")

    lines.extend(
        [
            "};",
            "",
            "}  // namespace ajazz::tests::fixtures",
            "",
        ],
    )
    return "\n".join(lines)


# --------------------------------------------------------------------------
# Entry point
# --------------------------------------------------------------------------
def _read_input(path_str: str) -> str:
    if path_str == "-":
        return sys.stdin.read()
    path = Path(path_str)
    if path.suffix.lower() in {".pcap", ".pcapng"}:
        msg = (
            f"{path}: refusing to read a raw capture file directly. "
            f"Run 'usbrply -j {path} > out.json' first and pass out.json. "
            f"See docs/protocols/CAPTURING.md (CAPTURE-01 hygiene policy)."
        )
        raise ValueError(msg)
    return path.read_text(encoding="utf-8")


def main(argv: list[str]) -> int:
    try:
        args = parse_args(argv)
        device_upper = normalise_device(args.device)
        label_upper = normalise_label(args.capture)
        raw = _read_input(args.input)
        doc = json.loads(raw)
        if not isinstance(doc, dict):
            msg = "usbrply JSON top-level must be an object with a 'packs' key"
            raise ValueError(msg)
        payload = select_packet(doc, args.packet_index, args.type_filter)
    except FileNotFoundError as exc:
        print(f"hex-to-cpparray: input not found: {exc}", file=sys.stderr)
        return 2
    except json.JSONDecodeError as exc:
        print(f"hex-to-cpparray: invalid JSON: {exc}", file=sys.stderr)
        return 2
    except ValueError as exc:
        print(f"hex-to-cpparray: {exc}", file=sys.stderr)
        return 2

    sys.stdout.write(emit_header(device_upper, label_upper, payload, args.device, args.capture))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
