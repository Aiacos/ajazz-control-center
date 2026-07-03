#!/usr/bin/env python3
"""AKP05E touch-strip / encoder surface mapper (BAT wire bytes 1..5).

The probe (akp05_color_probe.py) renders every surface via the BAT opcode with
a physical wire byte: 1..4 = encoders, 5 = touch strip, 6..15 = keys. The app
instead drives the strip via MAI (blank on hardware) and encoders via ENC. This
script paints LARGE distinct colour+label images to BAT wire bytes 1..6 ONLY so
the owner can report exactly where each lands physically — mapping the real
encoder/strip layout before wiring the app.

Each wire byte gets a solid colour and a big white number. wire 6 (first bottom
key) is included as a position anchor relative to the strip.

  --size N    square edge in px (default 85; try 200 to see if wire 5 fills more)
  --rot 0|180 per-image rotation (default 180, matching the validated key fix)

Defensive output-only diagnostic for the owner's own hardware. No input read.
"""

from __future__ import annotations

import argparse
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

PACKET = 1024
COLORS = {
    1: (230, 60, 60),
    2: (60, 200, 90),
    3: (60, 120, 230),
    4: (230, 200, 50),
    5: (200, 60, 220),
    6: (60, 210, 210),
}


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


def make_square(label: str, color, size: int, rot: int) -> bytes:
    img = Image.new("RGB", (size, size), color)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, size - 1, size - 1], outline=(0, 0, 0), width=3)
    try:
        font = ImageFont.truetype(
            "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf", int(size * 0.5)
        )
    except OSError:
        font = ImageFont.load_default()
    bbox = d.textbbox((0, 0), label, font=font)
    d.text(
        ((size - (bbox[2] - bbox[0])) / 2, (size - (bbox[3] - bbox[1])) / 2 - 2),
        label,
        fill=(255, 255, 255),
        font=font,
    )
    if rot:
        img = img.rotate(rot)
    buf = io.BytesIO()
    img.save(buf, format="JPEG", quality=85)
    return buf.getvalue()


def bat_header(wire: int, n: int) -> bytes:
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
                (n >> 8) & 0xFF,
                n & 0xFF,
                wire & 0xFF,
            ]
        )
    )


DIS = pad(bytes([0x00, 0x43, 0x52, 0x54, 0x00, 0x00, 0x44, 0x49, 0x53]))
ULEND = pad(bytes([0x00, 0x43, 0x52, 0x54, 0x55, 0x4C, 0x45, 0x4E, 0x44]))


def lig(p: int = 90) -> bytes:
    return pad(bytes([0x00, 0x43, 0x52, 0x54, 0x00, 0x00, 0x4C, 0x49, 0x47, 0x00, 0x00, p & 0xFF]))


def send(fd: int, wire: int, jpeg: bytes) -> None:
    os.write(fd, bat_header(wire, len(jpeg)))
    for i in range(0, len(jpeg), PACKET):
        os.write(fd, pad(bytes([0x00]) + jpeg[i : i + PACKET]))
    os.write(fd, ULEND)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--rot", type=int, default=180)
    ap.add_argument(
        "--sizes",
        default="128,152,176,200",
        help="comma list of 4 px sizes for strip zones wire 1..4",
    )
    args = ap.parse_args()
    sizes = [int(x) for x in args.sizes.split(",")]
    node = find_control_node()
    if not node:
        print("ERROR: no AKP05E 0xFFA0 control node.", file=sys.stderr)
        return 2
    fd = os.open(node, os.O_RDWR)
    print(f"# control node: {node}  zone sizes={sizes} rot={args.rot}")
    os.write(fd, DIS)
    os.write(fd, lig(90))
    time.sleep(0.05)
    # strip zones wire 1..4, each at a DIFFERENT size labelled with that px value
    for wire, sz in zip((1, 2, 3, 4), sizes, strict=False):
        send(fd, wire, make_square(str(sz), COLORS[wire], sz, args.rot))
        time.sleep(0.03)
    os.close(fd)
    print(f"# strip zones painted at sizes {sizes} (wire 1..4). Each square is labelled")
    print("# with its own pixel size. REPORT which size best fills its zone edge-to-edge.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
