#!/usr/bin/env python3
"""AKP05E per-key margin diagnostic — is the "keys 2-10 right-margin" a code bug
or a 0x3004 demo-unit firmware quirk?

Background
----------
Our key path was cross-checked (2026-06-01) against the authoritative
``ambiso/opendeck-akp05`` reference and matches it exactly:

    * key image  = 112x112 JPEG, Rot180        (mappings.rs:166)
    * top-row    logical 0..4 -> wire 11..15    (device.rs map_position + mirajazz key+1)
    * bottom-row logical 5..9 -> wire 6..10
    * encoders   0..3        -> wire 1..4

opendeck uses a UNIFORM 112x112 for every key and runs on retail N4 hardware.
Yet on the live 0x3004 "HOTSPOTEKUSB HID DEMO" unit, key 1 fills 1:1 at 112 but
keys 2-10 show a right-margin. Since our encode is byte-identical per key and the
BAT key-image header carries NO geometry (only JPEG size + wire byte), the only
per-key variable is the wire byte at offset 13 -> i.e. the firmware's per-LCD
behaviour. This probe makes that conclusion (or refutes it) directly observable.

Two modes
---------
* DEFAULT (decisive): build ONE 112x112 JPEG and send the *byte-identical* buffer
  to all 10 keys (wire 11..15, 6..10). The image has a bright border touching all
  four edges + corner-to-corner diagonals, so any clipping/margin is obvious and
  you can read off WHICH edge it is. If key 1 fills and keys 2-10 margin with
  provably-identical bytes, it is the firmware/per-wire-byte LCD = demo-unit quirk
  (do NOT "fix" by diverging from the retail-correct 112). Photograph the panel.

* --sweep: paint ascending image sizes per key (112,116,120,...) each labelled
  with its px size, so IF the LCDs are uniform and simply want a different size,
  one photo shows which size fills the key. (Conflates if LCDs differ per key.)

Wire format is the live-confirmed AKP05E framing (mirajazz / cc04a54 + 037bd8d),
identical to scripts/akp05_color_probe.py. DEFENSIVE output-only diagnostic for
the device owner's own hardware. See docs/protocols/streamdeck/akp05.md.

Usage:
    python3 scripts/akp05_key_margin_probe.py                 # identical 112 to all keys
    python3 scripts/akp05_key_margin_probe.py --size 120      # identical 120 to all keys
    python3 scripts/akp05_key_margin_probe.py --sweep         # 112,116,120,... per key
    python3 scripts/akp05_key_margin_probe.py --node /dev/hidraw13
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
    print("ERROR: Pillow is required (pip install pillow).", file=sys.stderr)
    raise SystemExit(2) from None

VID, PID = 0x0300, 0x3004
PACKET = 1024  # protocol_version 3 OUT endpoint payload size
KEY_SIZE = 112  # authoritative opendeck-akp05 mappings.rs:166 (uniform, all keys)


# logical key (1-based) -> wire byte: top row 1..5 -> 11..15, bottom 6..10 -> 6..10
def key_logical_to_wire(k: int) -> int:
    return (10 + k) if k <= 5 else k


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
        if "0300:00003004" not in txt and "00000300:00003004" not in txt:
            continue
        try:
            desc = (entry / "device" / "report_descriptor").read_bytes()
        except OSError:
            desc = b""
        node = f"/dev/{entry.name}"
        if desc[:3] == bytes([0x06, 0xA0, 0xFF]):  # vendor control collection 0xFFA0
            return node
        cand.append(node)
    return cand[0] if cand else None


def make_target(size: int, label: str) -> bytes:
    """A test square whose border touches all 4 edges, so clipping/margin is visible.

    Drawn upright then rotated 180 deg (the panel mounts every LCD inverted, so the
    app pre-rotates 180 before encoding — match it). Elements, by purpose:
      * 4px MAGENTA outer border ON the very edge  -> any missing side == clip/margin there
      * 2px GREEN frame inset 8px                  -> secondary reference
      * CYAN diagonals corner-to-corner            -> scale-vs-clip tell (clip = stop short)
      * YELLOW bar along ONE edge (the TOP, upright) -> asymmetry marker for "which side"
      * white centred label (e.g. "112")           -> photo self-documents the size
    """
    img = Image.new("RGB", (size, size), (28, 28, 32))
    d = ImageDraw.Draw(img)
    d.line([(0, 0), (size - 1, size - 1)], fill=(0, 200, 220), width=1)
    d.line([(size - 1, 0), (0, size - 1)], fill=(0, 200, 220), width=1)
    d.rectangle([4, 4, size - 5, size - 5], outline=(40, 220, 90), width=2)
    d.rectangle([0, 0, size - 1, size - 1], outline=(235, 40, 200), width=4)
    d.rectangle([0, 0, size - 1, 7], fill=(240, 210, 30))  # yellow = TOP edge marker
    try:
        font = ImageFont.truetype("/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf", 30)
    except OSError:
        font = ImageFont.load_default()
    bbox = d.textbbox((0, 0), label, font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    d.text(((size - tw) / 2, (size - th) / 2 - 2), label, fill=(255, 255, 255), font=font)
    img = img.transpose(Image.Transpose.ROTATE_180)  # panel is mounted inverted
    buf = io.BytesIO()
    img.save(buf, format="JPEG", quality=85)
    return buf.getvalue()


def pad(pkt: bytes) -> bytes:
    return pkt + bytes((1 + PACKET) - len(pkt))


def bat_header(wire_byte: int, jpeg_len: int) -> bytes:
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


def image_chunks(jpeg: bytes) -> list[bytes]:
    return [pad(bytes([0x00]) + jpeg[i : i + PACKET]) for i in range(0, len(jpeg), PACKET)]


ULEND = pad(bytes([0x00, 0x43, 0x52, 0x54, 0x55, 0x4C, 0x45, 0x4E, 0x44]))
CONNECT = pad(bytes([0x00, 0x43, 0x52, 0x54, 0x00, 0x00, 0x43, 0x4F, 0x4E, 0x4E, 0x45, 0x43, 0x54]))


def lig(percent: int = 80) -> bytes:
    return pad(
        bytes([0x00, 0x43, 0x52, 0x54, 0x00, 0x00, 0x4C, 0x49, 0x47, 0x00, 0x00, percent & 0xFF])
    )


def send_image(fd: int, wire_byte: int, jpeg: bytes) -> None:
    os.write(fd, bat_header(wire_byte, len(jpeg)))
    for chunk in image_chunks(jpeg):
        os.write(fd, chunk)
    os.write(fd, ULEND)


def main() -> int:
    ap = argparse.ArgumentParser(description="AKP05E per-key margin diagnostic")
    ap.add_argument("--node", help="control hidraw node (default: auto 0xFFA0)")
    ap.add_argument("--size", type=int, default=KEY_SIZE, help=f"image px (default {KEY_SIZE})")
    ap.add_argument("--sweep", action="store_true", help="ascending size per key (112,116,...)")
    ap.add_argument("--brightness", type=int, default=80)
    args = ap.parse_args()

    ctrl = args.node or find_control_node()
    if not ctrl:
        print(
            "ERROR: no AKP05E (0300:3004) vendor-control node found. Plugged in?", file=sys.stderr
        )
        return 2
    try:
        fd = os.open(ctrl, os.O_RDWR | os.O_NONBLOCK)
    except OSError as exc:
        print(
            f"ERROR: cannot open {ctrl}: {exc} (uaccess ACL? replug, or "
            f"sudo setfacl -m u:$(id -u):rw {ctrl})",
            file=sys.stderr,
        )
        return 2

    # NO `CRT DIS` — it wedges the display (CLAUDE.md render-model hard rule); the
    # vendor open() sends only VER, the service owns LIG. CONNECT is the safe wake.
    os.write(fd, CONNECT)
    os.write(fd, lig(args.brightness))
    time.sleep(0.05)

    try:
        if args.sweep:
            # Ascending sizes so one photo reveals which size fills a (uniform) LCD.
            sizes = [112, 116, 120, 124, 128, 112, 118, 124, 130, 136]
            print(f"# control node: {ctrl}")
            print("# SWEEP: each key painted at a different size (label = px). Photograph it.")
            for k in range(1, 11):
                s = sizes[k - 1]
                jpeg = make_target(s, str(s))
                send_image(fd, key_logical_to_wire(k), jpeg)
                print(f"#   K{k:<2} (wire {key_logical_to_wire(k):>2}) <- {s}x{s}  ({len(jpeg)} B)")
                time.sleep(0.02)
        else:
            # DECISIVE: one buffer, byte-identical, to every key.
            jpeg = make_target(args.size, str(args.size))
            digest = sum(jpeg) & 0xFFFFFFFF
            print(f"# control node: {ctrl}")
            print(
                f"# DECISIVE: byte-identical {args.size}x{args.size} JPEG "
                f"({len(jpeg)} B, checksum 0x{digest:08x}) -> ALL 10 keys."
            )
            print("# If key 1 fills 1:1 but keys 2-10 show a right-margin with THIS identical")
            print("# buffer, the firmware/per-wire-byte LCD is the only variable = demo quirk.")
            for k in range(1, 11):
                send_image(fd, key_logical_to_wire(k), jpeg)  # same `jpeg` object every time
                print(f"#   K{k:<2} (wire {key_logical_to_wire(k):>2}) <- identical buffer")
                time.sleep(0.02)
        print("# done. Photograph the panel; report per key: does the MAGENTA border touch")
        print("#       all 4 edges? if not, which edge has the gap (yellow bar = top, upright)?")
    finally:
        os.close(fd)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
