#!/usr/bin/env python3
"""Interactive AKP05E color-square round-trip harness (output + input proof).

Sends a distinct, labelled, color-coded JPEG square to *every* physical surface
of an AKP05E / Mirabox-N4 Stream Dock Plus:

    * 4 rotary encoders ("DIAL 1..4")      -> wire bytes 1..4
    * touch strip       ("TOUCH")          -> wire byte  5
    * bottom-row keys   ("K6..K10")        -> wire bytes 6..10
    * top-row keys      ("K1..K5")         -> wire bytes 11..15

Then it reads the vendor HID input channel and, when a control reports an
event, CLEARs that control's square. A square that vanishes == that control's
output AND input round-trip is confirmed end to end. Every raw input frame is
logged (``report[9]`` action code + ``report[10]`` context byte + full hex) so
that, on a unit whose input path actually streams, the exact encoder/touch
codes are captured directly from hardware (turning the PROVISIONAL values in
``akp05_input_corrections.md`` §3/§4 into confirmed ones).

Wire format is the live-confirmed AKP05E framing (mirajazz / commit cc04a54 +
037bd8d): POSIX ``0x00`` report-id prepend, "CRT" at offset 1, opcode at 6,
BE16 JPEG size at 11..12, key/wire byte at 13, 1024-byte payload chunks padded
to 1025, "ULEND" commit after each image; CRT DIS + CRT LIG init; CRT CONNECT
keep-alive at ~1 s. See docs/protocols/streamdeck/akp05_input_corrections.md.

This is a DEFENSIVE round-trip diagnostic for the device owner's own hardware.
It captures control-channel button/encoder/touch reports only.

Usage:
    python3 scripts/akp05_color_probe.py                 # auto-detect, run 75 s
    python3 scripts/akp05_color_probe.py --seconds 120
    python3 scripts/akp05_color_probe.py --node /dev/hidraw13 --log /tmp/akp05.log
"""

from __future__ import annotations

import argparse
import contextlib
import io
import os
import select
import signal
import sys
import time
from collections import defaultdict
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("ERROR: Pillow is required (pip install pillow).", file=sys.stderr)
    raise SystemExit(2) from None

VID, PID = 0x0300, 0x3004
PACKET = 1024  # protocol_version 3 OUT endpoint payload size

# ---- physical-surface wire map (037bd8d, hardware-confirmed) ---------------
# BAT byte 13 addresses a physical surface, NOT a linear key index:
#   1..4 = encoder LCDs,  5 = touch strip,  6..10 = bottom row,  11..15 = top row.
ENCODER_WIRE = [1, 2, 3, 4]  # dial 1..4
STRIP_WIRE = 5


# logical key (1-based) -> wire byte: top row 1..5 -> 11..15, bottom 6..10 -> 6..10
def key_logical_to_wire(k: int) -> int:
    return (10 + k) if k <= 5 else k


# colours per surface class (R,G,B)
COL_DIAL = (40, 110, 230)  # blue
COL_TOUCH = (40, 190, 90)  # green
COL_TOPKEY = (235, 140, 30)  # orange
COL_BOTKEY = (215, 55, 55)  # red

# ---- input decode (NEW code map; encoder index/polarity still PROVISIONAL) --
# encoder rotation report[9] -> encoder index (0-based). odd code = CW.
ENC_ROT = {0xA0: 0, 0xA1: 0, 0x50: 1, 0x51: 1, 0x90: 2, 0x91: 2, 0x70: 3, 0x71: 3}
# encoder press report[9] -> encoder index (0-based).
ENC_PRESS = {0x37: 0, 0x35: 1, 0x33: 2, 0x36: 3}
TOUCH_CODES = {0x97: "move", 0x98: "down", 0x99: "up"}


def find_control_node() -> str | None:
    """Return the vendor-control hidraw node (Usage Page 0xFFA0, interface 00)."""
    base = Path("/sys/class/hidraw")
    if not base.is_dir():
        return None
    cand: list[str] = []
    for entry in sorted(base.iterdir()):
        try:
            txt = (entry / "device" / "uevent").read_text()
        except OSError:
            continue
        if (
            f"{VID:08X}:{PID:08X}".lower() not in txt.replace("0000", "").lower()
            and "0300:00003004" not in txt
            and "00000300:00003004" not in txt
        ):
            continue
        try:
            desc = (entry / "device" / "report_descriptor").read_bytes()
        except OSError:
            desc = b""
        node = f"/dev/{entry.name}"
        # vendor control collection starts with Usage Page 0xFFA0 (06 a0 ff)
        if desc[:3] == bytes([0x06, 0xA0, 0xFF]):
            return node
        cand.append(node)
    return cand[0] if cand else None


def find_input_nodes() -> list[str]:
    out: list[str] = []
    for entry in sorted(Path("/sys/class/hidraw").iterdir()):
        try:
            txt = (entry / "device" / "uevent").read_text()
        except OSError:
            continue
        if "0300:00003004" in txt or "00000300:00003004" in txt or "0300:3004" in txt.lower():
            out.append(f"/dev/{entry.name}")
    return out


def make_square(label: str, color: tuple[int, int, int], size: int = 85) -> bytes:
    """Return an 85x85 JPEG: solid colour, dark border, white label text."""
    img = Image.new("RGB", (size, size), color)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, size - 1, size - 1], outline=(0, 0, 0), width=3)
    try:
        font = ImageFont.truetype("/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf", 18)
    except OSError:
        font = ImageFont.load_default()
    bbox = d.textbbox((0, 0), label, font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    d.text(((size - tw) / 2, (size - th) / 2 - 2), label, fill=(255, 255, 255), font=font)
    buf = io.BytesIO()
    img.save(buf, format="JPEG", quality=85)
    return buf.getvalue()


def pad(pkt: bytes) -> bytes:
    """0x00 report-id already present; pad the whole frame to 1 + PACKET bytes."""
    return pkt + bytes((1 + PACKET) - len(pkt))


def bat_header(wire_byte: int, jpeg_len: int) -> bytes:
    return pad(
        bytes(
            [
                0x00,  # report-id
                0x43,
                0x52,
                0x54,  # "CRT"
                0x00,
                0x00,
                0x42,
                0x41,
                0x54,  # "BAT"
                0x00,
                0x00,
                (jpeg_len >> 8) & 0xFF,  # BE16 size hi
                jpeg_len & 0xFF,  # BE16 size lo
                wire_byte & 0xFF,  # surface wire byte
            ]
        )
    )


def image_chunks(jpeg: bytes) -> list[bytes]:
    out = []
    for i in range(0, len(jpeg), PACKET):
        out.append(pad(bytes([0x00]) + jpeg[i : i + PACKET]))
    return out


ULEND = pad(
    bytes([0x00, 0x43, 0x52, 0x54, 0x55, 0x4C, 0x45, 0x4E, 0x44])
)  # CRT ULEND (5-byte word at 5..9)
DIS = pad(bytes([0x00, 0x43, 0x52, 0x54, 0x00, 0x00, 0x44, 0x49, 0x53]))  # CRT DIS
CONNECT = pad(bytes([0x00, 0x43, 0x52, 0x54, 0x00, 0x00, 0x43, 0x4F, 0x4E, 0x4E, 0x45, 0x43, 0x54]))


def lig(percent: int = 80) -> bytes:
    return pad(
        bytes([0x00, 0x43, 0x52, 0x54, 0x00, 0x00, 0x4C, 0x49, 0x47, 0x00, 0x00, percent & 0xFF])
    )


def clear_key(wire_byte: int) -> bytes:
    # CLE: opcode at 6..8 (with prepend), wire byte at offset 12.
    return pad(
        bytes(
            [
                0x00,
                0x43,
                0x52,
                0x54,
                0x00,
                0x00,
                0x43,
                0x4C,
                0x45,
                0x00,
                0x00,
                0x00,
                wire_byte & 0xFF,
            ]
        )
    )


def send_image(fd: int, wire_byte: int, jpeg: bytes) -> None:
    os.write(fd, bat_header(wire_byte, len(jpeg)))
    for chunk in image_chunks(jpeg):
        os.write(fd, chunk)
    os.write(fd, ULEND)


def fmt(buf: bytes, width: int = 16) -> str:
    return " ".join(f"{b:02x}" for b in buf[:width])


def build_surfaces() -> dict[int, str]:
    """wire_byte -> human label for every square we paint."""
    surf: dict[int, str] = {}
    for e in range(4):
        surf[ENCODER_WIRE[e]] = f"DIAL {e + 1}"
    surf[STRIP_WIRE] = "TOUCH"
    for k in range(1, 11):
        surf[key_logical_to_wire(k)] = f"K{k}"
    return surf


def color_for(wire_byte: int) -> tuple[int, int, int]:
    if wire_byte in ENCODER_WIRE:
        return COL_DIAL
    if wire_byte == STRIP_WIRE:
        return COL_TOUCH
    if 11 <= wire_byte <= 15:
        return COL_TOPKEY
    return COL_BOTKEY


def main() -> int:  # noqa: PLR0912, PLR0915 - linear operator diagnostic; clarity beats decomposition
    ap = argparse.ArgumentParser(description="AKP05E color-square round-trip harness")
    ap.add_argument("--node", help="control hidraw node (default: auto 0xFFA0)")
    ap.add_argument("--seconds", type=float, default=75.0, help="capture window (default 75)")
    ap.add_argument("--log", help="append every decoded frame to this file")
    ap.add_argument("--brightness", type=int, default=80)
    args = ap.parse_args()

    ctrl = args.node or find_control_node()
    if not ctrl:
        print(
            "ERROR: no AKP05E (0300:3004) vendor-control node found. Plugged in?", file=sys.stderr
        )
        return 2
    read_nodes = sorted({ctrl, *find_input_nodes()})

    try:
        wfd = os.open(ctrl, os.O_RDWR | os.O_NONBLOCK)
    except OSError as exc:
        print(
            f"ERROR: cannot open {ctrl}: {exc} (uaccess ACL? replug if root-only)", file=sys.stderr
        )
        return 2

    rfds: dict[int, str] = {wfd: ctrl}
    for node in read_nodes:
        if node == ctrl:
            continue
        with contextlib.suppress(OSError):
            rfds[os.open(node, os.O_RDONLY | os.O_NONBLOCK)] = node

    surfaces = build_surfaces()
    print(f"# control node: {ctrl}")
    print(f"# reading: {', '.join(rfds.values())}")
    print(f"# painting {len(surfaces)} squares: 4 dials + touch + 10 keys")

    # ---- init + paint -----------------------------------------------------
    os.write(wfd, DIS)
    os.write(wfd, lig(args.brightness))
    time.sleep(0.05)
    for wire_byte, label in sorted(surfaces.items()):
        jpeg = make_square(label, color_for(wire_byte))
        try:
            send_image(wfd, wire_byte, jpeg)
        except OSError as exc:
            print(f"# write error painting wire {wire_byte} ({label}): {exc}", file=sys.stderr)
        time.sleep(0.02)
    print("# squares painted. PRESS each key, TURN + PRESS each dial, TAP/SWIPE the strip.")
    print("# a square that disappears == that control's round-trip is confirmed.")
    print(f"#  {'t(s)':>7}  node          len  code@9 ctx@10  decoded            bytes[0..15]")

    # Held open across the whole capture loop (closed in the finally) — a context
    # manager would not span the loop, so a long-lived handle is intentional.
    logfh = Path(args.log).open("a") if args.log else None  # noqa: SIM115
    seen: dict[tuple[int, int], dict] = defaultdict(lambda: {"count": 0, "lens": set()})
    cleared: set[int] = set()
    stop = {"flag": False}
    signal.signal(signal.SIGINT, lambda *_: stop.update(flag=True))

    start = time.monotonic()
    deadline = start + args.seconds
    last_connect = 0.0
    try:
        while not stop["flag"] and time.monotonic() < deadline:
            now = time.monotonic()
            if now - last_connect >= 1.0:
                with contextlib.suppress(OSError):
                    os.write(wfd, CONNECT)
                last_connect = now
            r, _, _ = select.select(list(rfds), [], [], 0.2)
            for fd in r:
                try:
                    buf = os.read(fd, 1024)
                except (BlockingIOError, OSError):
                    continue
                if len(buf) < 11:
                    continue
                if (
                    buf[0:3] == b"ACK"
                ):  # ACK gate (akp03-consistent; see akp05_input_corrections §2)
                    continue
                code, ctx = buf[9], buf[10]
                t = now - start
                decoded, clear_wire = decode(code, ctx)
                if clear_wire is not None and clear_wire in surfaces and clear_wire not in cleared:
                    try:
                        os.write(wfd, clear_key(clear_wire))
                        cleared.add(clear_wire)
                        decoded += f" -> CLEARED {surfaces[clear_wire]}"
                    except OSError:
                        pass
                line = (
                    f"  {t:7.3f}  {Path(rfds[fd]).name:11}  {len(buf):3d}   "
                    f"0x{code:02x}  0x{ctx:02x}   {decoded:18}  {fmt(buf)}"
                )
                print(line, flush=True)
                if logfh:
                    logfh.write(line + "\n")
                    logfh.flush()
                rec = seen[(code, ctx)]
                rec["count"] += 1
                rec["lens"].add(len(buf))
            if len(cleared) == len(surfaces):
                print("# ALL squares cleared — full bidirectional round-trip confirmed.")
                break
    finally:
        for fd in rfds:
            os.close(fd)
        if logfh:
            logfh.close()

    # ---- summary ----------------------------------------------------------
    print("\n# ==== distinct (code@9, ctx@10) tuples observed ====")
    if seen:
        print("#  code  ctx   count  len(s)")
        for (code, ctx), rec in sorted(seen.items()):
            lens = ",".join(str(x) for x in sorted(rec["lens"]))
            print(f"   0x{code:02x}  0x{ctx:02x}  {rec['count']:5d}  {lens:>6}")
    else:
        print("#  NOTHING captured on any node.")
        print("#  -> output (squares) works but the input path is silent on this unit,")
        print("#     re-confirming the demo-unit input-unreachable finding (CLAUDE.md glossary).")
    painted, hit = len(surfaces), len(cleared)
    print(f"\n# round-trip: {hit}/{painted} squares cleared.")
    if hit == 0:
        print("# verdict: OUTPUT confirmed (squares rendered); INPUT not reachable on this unit.")
    elif hit < painted:
        print("# verdict: partial input — see which labels cleared above.")
    else:
        print("# verdict: full bidirectional comms confirmed.")
    return 0


def decode(code: int, ctx: int) -> tuple[str, int | None]:
    """Return (human label, wire byte to clear or None) for an input code."""
    if 1 <= code <= 10:
        return (f"KEY {code} {'down' if ctx else 'up'}", key_logical_to_wire(code))
    if code in ENC_ROT:
        e = ENC_ROT[code]
        return (f"DIAL{e + 1} {'CW' if code & 1 else 'CCW'}", ENCODER_WIRE[e])
    if code in ENC_PRESS:
        e = ENC_PRESS[code]
        return (f"DIAL{e + 1} press", ENCODER_WIRE[e])
    if code in TOUCH_CODES:
        return (f"TOUCH {TOUCH_CODES[code]} x={ctx}", STRIP_WIRE)
    return ("?unknown", None)


if __name__ == "__main__":
    raise SystemExit(main())
