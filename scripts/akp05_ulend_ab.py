#!/usr/bin/env python3
"""AKP05E A/B diagnostic: does the ULEND commit-sentinel OFFSET decide rendering?

Two RE sources disagree on where the 5-byte "ULEND" commit word sits in the
1024-byte vendor packet:

  * vendor Ghidra (akp05_vendor.md §"ULEND (5-byte) 5..9")  -> buffer offset 5..9
      => on the wire (after the 0x00 report-id prepend): CRT, 00 00, ULEND
      => this is what the app's akp05::buildUploadFinished() emits.
  * mirajazz / scripts/akp05_color_probe.py                 -> right after CRT
      => on the wire: CRT, ULEND  (no 00 00 gap; ULEND begins at wire offset 4)

The probe (offset-3 form) renders 15/15 on the live unit; the app (offset-5
form) shows the default logo. This script paints with BOTH forms in ONE device
session so the eye decides which the firmware actually commits:

  * APP-form ULEND  -> 3 RED squares on the TOP row    (wire bytes 11,12,13)
  * PROBE-form ULEND-> 3 GREEN squares on the BOTTOM row(wire bytes 6,7,8)

Read-off:
  RED top squares show    => app's ULEND offset is fine; the bug is elsewhere.
  Only GREEN bottom show   => ULEND offset is the bug; fix buildUploadFinished.
  Neither shows            => device re-wedged (replug) or a different cause.

Defensive single-purpose diagnostic for the owner's own hardware. No input read.
"""

from __future__ import annotations

import io
import os
import sys
import time
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("ERROR: Pillow required (pip install pillow).", file=sys.stderr)
    raise SystemExit(2) from None

VID, PID = 0x0300, 0x3004
PACKET = 1024


def find_control_node() -> str | None:
    for entry in sorted(Path("/sys/class/hidraw").iterdir()):
        try:
            txt = (entry / "device" / "uevent").read_text()
        except OSError:
            continue
        if "0300:00003004" not in txt and "00000300:00003004" not in txt:
            continue
        try:
            desc = (entry / "device" / "report_descriptor").read_bytes()
        except OSError:
            desc = b""
        if desc[:3] == bytes([0x06, 0xA0, 0xFF]):
            return f"/dev/{entry.name}"
    return None


def pad(pkt: bytes) -> bytes:
    return pkt + bytes((1 + PACKET) - len(pkt))


def make_square(label: str, color: tuple[int, int, int], size: int = 85) -> bytes:
    img = Image.new("RGB", (size, size), color)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, size - 1, size - 1], outline=(0, 0, 0), width=3)
    try:
        font = ImageFont.truetype("/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf", 22)
    except OSError:
        font = ImageFont.load_default()
    bbox = d.textbbox((0, 0), label, font=font)
    d.text(
        ((size - (bbox[2] - bbox[0])) / 2, (size - (bbox[3] - bbox[1])) / 2 - 2),
        label,
        fill=(255, 255, 255),
        font=font,
    )
    buf = io.BytesIO()
    img.save(buf, format="JPEG", quality=85)
    return buf.getvalue()


def bat_header(wire_byte: int, jpeg_len: int) -> bytes:
    # identical to the app's buildKeyImageHeader after the 0x00 report-id prepend
    return pad(
        bytes(
            [
                0x00,
                0x43,
                0x52,
                0x54,
                0x00,
                0x00,
                0x42,
                0x41,
                0x54,
                0x00,
                0x00,
                (jpeg_len >> 8) & 0xFF,
                jpeg_len & 0xFF,
                wire_byte & 0xFF,
            ]
        )
    )


# ULEND, APP form: CRT, 00 00, ULEND  -> ULEND begins at wire offset 6 (buffer 5)
ULEND_APP = pad(bytes([0x00, 0x43, 0x52, 0x54, 0x00, 0x00, 0x55, 0x4C, 0x45, 0x4E, 0x44]))
# ULEND, PROBE form: CRT, ULEND       -> ULEND begins at wire offset 4 (buffer 3)
ULEND_PROBE = pad(bytes([0x00, 0x43, 0x52, 0x54, 0x55, 0x4C, 0x45, 0x4E, 0x44]))

DIS = pad(bytes([0x00, 0x43, 0x52, 0x54, 0x00, 0x00, 0x44, 0x49, 0x53]))


def lig(p: int = 90) -> bytes:
    return pad(bytes([0x00, 0x43, 0x52, 0x54, 0x00, 0x00, 0x4C, 0x49, 0x47, 0x00, 0x00, p & 0xFF]))


def chunks(jpeg: bytes) -> list[bytes]:
    return [pad(bytes([0x00]) + jpeg[i : i + PACKET]) for i in range(0, len(jpeg), PACKET)]


def send(fd: int, wire: int, jpeg: bytes, ulend: bytes) -> None:
    os.write(fd, bat_header(wire, len(jpeg)))
    for c in chunks(jpeg):
        os.write(fd, c)
    os.write(fd, ulend)


def main() -> int:
    node = find_control_node()
    if not node:
        print("ERROR: no AKP05E 0xFFA0 control node (plugged in?).", file=sys.stderr)
        return 2
    fd = os.open(node, os.O_RDWR)
    print(f"# control node: {node}")
    os.write(fd, DIS)
    os.write(fd, lig(90))
    time.sleep(0.05)
    # APP-form ULEND -> RED squares on top row (wire 11,12,13)
    for wire, lab in ((11, "A1"), (12, "A2"), (13, "A3")):
        send(fd, wire, make_square(lab, (220, 40, 40)), ULEND_APP)
        time.sleep(0.03)
    # PROBE-form ULEND -> GREEN squares on bottom row (wire 6,7,8)
    for wire, lab in ((6, "P6"), (7, "P7"), (8, "P8")):
        send(fd, wire, make_square(lab, (40, 200, 90)), ULEND_PROBE)
        time.sleep(0.03)
    os.close(fd)
    print("# painted: RED A1/A2/A3 (top, APP-ULEND) + GREEN P6/P7/P8 (bottom, PROBE-ULEND)")
    print("# READ-OFF: RED top => app ULEND ok; only GREEN bottom => ULEND offset is the bug.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
