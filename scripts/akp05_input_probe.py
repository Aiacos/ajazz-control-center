#!/usr/bin/env python3
"""AKP05E (0300:3004) vendor-HID input-capture probe.

Reads raw input reports from the AKP05E Stream Dock Plus vendor HID
interface (MI_00, usage page 0xFFA0) on Linux hidraw and records
``report[9]`` (action code) + ``report[10]`` (context byte) per the
finalisation plan in ``docs/protocols/streamdeck/akp05_input_corrections.md``
§7. The goal: pin the [PROVISIONAL] encoder direction/press codes and the
touch-strip code + X scale that the Ghidra dossier could not.

This captures **control-channel button/encoder/touch reports only** (a
Stream Dock has LCD keys, not a text keyboard), so it is safe to sanitise
into committable ``std::array<uint8_t>`` fixtures per CAPTURE-01 (Pitfall 17).
Do NOT commit raw output that includes anything other than these reports.

Usage:
    python3 scripts/akp05_input_probe.py                 # auto-find hidraw, run until Ctrl-C
    python3 scripts/akp05_input_probe.py --device /dev/hidraw16
    python3 scripts/akp05_input_probe.py --seconds 60 --log /tmp/akp05_capture.log

On exit (Ctrl-C or --seconds deadline) it prints a deduplicated summary
table of every distinct (code @9, context @10) tuple observed, with counts
and the report length — that table is the thing to read off into the
``kEncoderCodes`` / ``lookupTouchCode`` tables.
"""

from __future__ import annotations

import argparse
import contextlib
import os
import select
import signal
import sys
import time
from collections import defaultdict
from pathlib import Path

VID = 0x0300
PID = 0x3004
VENDOR_USAGE_PAGE_PREFIX = bytes([0x06, 0xA0, 0xFF])  # Usage Page (Vendor 0xFFA0)


def find_device_hidraws() -> list[str]:
    """Return ALL hidraw nodes bound to 0300:3004 (both the vendor 0xFFA0
    interface MI_00 and the boot-keyboard interface MI_01). The AKP05E splits
    input across interfaces, so a capture must watch every node."""
    found: list[str] = []
    try:
        nodes = sorted(
            Path("/sys/class/hidraw").iterdir(), key=lambda p: int(p.name.removeprefix("hidraw"))
        )
    except OSError:
        return found
    for entry in nodes:
        try:
            uevent = (entry / "device" / "uevent").read_text().lower()
        except OSError:
            continue
        if "00000300:00003004" in uevent:
            found.append(f"/dev/{entry.name}")
    return found


def fmt(buf: bytes, width: int = 16) -> str:
    return " ".join(f"{b:02x}" for b in buf[:width])


def main() -> int:  # noqa: PLR0912, PLR0915 - linear operator diagnostic; clarity beats decomposition
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--device",
        action="append",
        default=[],
        help="hidraw node(s); repeatable. Default: auto-detect ALL 0300:3004 nodes.",
    )
    ap.add_argument(
        "--seconds",
        type=float,
        default=0.0,
        help="auto-stop after N seconds (default: run until Ctrl-C)",
    )
    ap.add_argument("--log", help="also append every report line to this file")
    ap.add_argument(
        "--min-len",
        type=int,
        default=1,
        help="ignore reports shorter than this (default 1 = log everything)",
    )
    ap.add_argument(
        "--activate",
        action="store_true",
        help="on the FIRST node, send CRT VER (GET_FEATURE id 0x01) + a LIG "
        "brightness on the SAME handle before reading, mirroring the vendor "
        "open->write->read pattern that puts the device in the streaming state",
    )
    args = ap.parse_args()

    devs = args.device or find_device_hidraws()
    if not devs:
        print(
            "ERROR: no AKP05E (0300:3004) hidraw node found. Is it plugged in? "
            "Pass --device explicitly.",
            file=sys.stderr,
        )
        return 2

    fd2name: dict[int, str] = {}
    # O_RDWR so --activate can write on the same handle it reads from (vendor pattern).
    for dev in devs:
        try:
            fd2name[os.open(dev, os.O_RDWR | os.O_NONBLOCK)] = dev
        except OSError:
            # fall back to read-only if RW is refused
            try:
                fd2name[os.open(dev, os.O_RDONLY | os.O_NONBLOCK)] = dev
            except OSError as exc2:
                print(f"# WARN: cannot open {dev}: {exc2}", file=sys.stderr)

    if args.activate and fd2name:
        # Mirror the vendor open sequence EXACTLY (akp05_init_sequence.md §3.2):
        # the one host->device packet sent before input is a CRT VER OUTPUT WRITE
        # on EP 0x03 (hid_write), on the SAME handle the read loop uses. Correct
        # Linux framing = 0x00 report-id prepend (device has no report IDs), CRT
        # at offset 0, opcode "VER" at offset 5 — identical framing to the LIG/CLE
        # writes that visibly worked.
        first = next(iter(fd2name))
        # EXACT mirajazz initialize() (the authoritative AKP05/N4 lib our backend
        # follows). The pieces our prior attempts MISSED: CRT DIS (display-init)
        # and the periodic CRT CONNECT keep-alive (sent in the read loop below).
        DIS = bytes([0x00, 0x43, 0x52, 0x54, 0x00, 0x00, 0x44, 0x49, 0x53])  # CRT DIS  # noqa: N806
        LIG = bytes([0x00, 0x43, 0x52, 0x54, 0x00, 0x00, 0x4C, 0x49, 0x47, 0, 0, 0, 0])  # noqa: N806

        def padded(b):
            return b + bytes(1 + 1024 - len(b))  # 1 + packet_size

        try:
            os.write(first, padded(DIS))
            print("# activate: CRT DIS (display-init) sent")
            os.write(first, padded(LIG))
            print("# activate: CRT LIG sent")
        except OSError as exc:
            print(f"# activate: init write failed: {exc}")
    if not fd2name:
        print(
            "ERROR: could not open any node (check uaccess ACL; replug if root-only).",
            file=sys.stderr,
        )
        return 2

    # Long-lived across the capture loop (closed in finally); a context manager
    # would not span the loop, so the open handle is intentional.
    logfh = Path(args.log).open("a") if args.log else None  # noqa: SIM115
    # key the dedup on (node, code@9, ctx@10) -- node matters since input is split
    seen: dict[tuple[str, int, int], dict] = defaultdict(
        lambda: {"count": 0, "lens": set(), "samples": []}
    )

    stop = {"flag": False}
    signal.signal(signal.SIGINT, lambda *_: stop.update(flag=True))

    deadline = time.monotonic() + args.seconds if args.seconds > 0 else None
    print(
        f"# capturing from {', '.join(fd2name.values())}  (Ctrl-C to stop"
        + (f", auto-stop in {args.seconds:g}s)" if deadline else ")")
    )
    print(
        "# press each control: 10 keys, then per encoder one CW / one CCW / one press, "
        "then touch down+move+up across the strip in each of the 4 zones."
    )
    print(f"#  {'t(s)':>7}  node          len  code@9 ctx@10  bytes[0..15]")

    # mirajazz keep-alive: CRT CONNECT, sent periodically on the first handle.
    connect = bytes([0x00, 0x43, 0x52, 0x54, 0x00, 0x00, 0x43, 0x4F, 0x4E, 0x4E, 0x45, 0x43, 0x54])
    connect = connect + bytes(1 + 1024 - len(connect))
    keepalive_fd = next(iter(fd2name)) if args.activate else None
    last_connect = 0.0

    start = time.monotonic()
    try:
        while not stop["flag"]:
            if deadline and time.monotonic() >= deadline:
                break
            if keepalive_fd is not None and time.monotonic() - last_connect >= 1.0:
                with contextlib.suppress(OSError):
                    os.write(keepalive_fd, connect)
                last_connect = time.monotonic()
            r, _, _ = select.select(list(fd2name), [], [], 0.25)
            for fd in r:
                try:
                    buf = os.read(fd, 1024)
                except BlockingIOError:
                    continue
                except OSError as exc:
                    print(f"# read error on {fd2name[fd]}: {exc}", file=sys.stderr)
                    buf = b""
                if len(buf) < args.min_len:
                    continue
                node = Path(fd2name[fd]).name
                code = buf[9] if len(buf) > 9 else -1
                ctx = buf[10] if len(buf) > 10 else -1
                t = time.monotonic() - start
                line = (
                    f"  {t:7.3f}  {node:11}  {len(buf):3d}   "
                    f"{(f'0x{code:02x}') if code >= 0 else '  - '}  "
                    f"{(f'0x{ctx:02x}') if ctx >= 0 else '  - '}   {fmt(buf, 16)}"
                )
                print(line, flush=True)
                if logfh:
                    logfh.write(line + "\n")
                    logfh.flush()
                rec = seen[(node, code, ctx)]
                rec["count"] += 1
                rec["lens"].add(len(buf))
                if len(rec["samples"]) < 2:
                    rec["samples"].append(fmt(buf, 24))
    finally:
        for fd in fd2name:
            os.close(fd)
        if logfh:
            logfh.close()

    print("\n# ==== distinct (node, code@9, ctx@10) tuples observed ====")
    print("#  node         code  ctx   count  len(s)  first-sample (bytes 0..23)")
    for (node, code, ctx), rec in sorted(seen.items()):
        lens = ",".join(str(x) for x in sorted(rec["lens"]))
        cs = (f"0x{code:02x}") if code >= 0 else " - "
        xs = (f"0x{ctx:02x}") if ctx >= 0 else " - "
        sample = rec["samples"][0] if rec["samples"] else ""
        print(f"   {node:11}  {cs}  {xs}  {rec['count']:5d}  {lens:>6}  {sample}")
    if not seen:
        print(
            "#  (nothing captured on any node — the device may need a fuller vendor init "
            "than a bare LIG; or input rides evdev event264. See diagnostics.)"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
