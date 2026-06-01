#!/usr/bin/env python3
"""AKP05E image-size diagnostic — paint a bordered test target to keys/strip zones.

Each target is a rect with a border on all 4 edges + corner diagonals + a px-size
label, Rot180 (the panel mounts inverted). Photograph the panel to read whether a
given size fills a surface 1:1 or leaves a margin. Used to confirm the 112x112 key
size (a byte-identical buffer fills every key) and the strip-zone size.

Wire format is the live-confirmed AKP05E BAT framing (mirajazz / cc04a54 + 037bd8d),
as in scripts/akp05_color_probe.py. Sends NO `CRT DIS` (it wedges the display);
inits with CONNECT + LIG. Output-only; for the device owner's own hardware.
See docs/protocols/streamdeck/akp05.md.

Usage:
    python3 scripts/akp05_key_margin_probe.py                 # identical 112 to all 10 keys
    python3 scripts/akp05_key_margin_probe.py --size 120      # identical size to all keys
    python3 scripts/akp05_key_margin_probe.py --sweep         # 112,116,120,... per key
    python3 scripts/akp05_key_margin_probe.py --zones --zw 128 --zh 128   # 4 strip zones
    python3 scripts/akp05_key_margin_probe.py --zsweep        # zone widths --widths x --zh
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
ENCODER_WIRE = [1, 2, 3, 4]  # 4 touch-strip zones aligned to encoders E1..E4
ZONE_W, ZONE_H = 128, 128  # established zone square (discrete, gaps by design); cosmetic


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


def make_target(w: int, h: int, label: str) -> bytes:
    """A test rect whose border touches all 4 edges, so clipping/margin is visible.

    Drawn upright then rotated 180 deg (the panel mounts every LCD inverted, so the
    app pre-rotates 180 before encoding — match it). Elements, by purpose:
      * 4px MAGENTA outer border ON the very edge  -> any missing side == clip/margin there
      * 2px GREEN frame inset 8px                  -> secondary reference
      * CYAN diagonals corner-to-corner            -> scale-vs-clip tell (clip = stop short)
      * YELLOW bar along ONE edge (the TOP, upright) -> asymmetry marker for "which side"
      * white centred label (e.g. "112" or "176x112") -> photo self-documents the size
    """
    img = Image.new("RGB", (w, h), (28, 28, 32))
    d = ImageDraw.Draw(img)
    d.line([(0, 0), (w - 1, h - 1)], fill=(0, 200, 220), width=1)
    d.line([(w - 1, 0), (0, h - 1)], fill=(0, 200, 220), width=1)
    d.rectangle([4, 4, w - 5, h - 5], outline=(40, 220, 90), width=2)
    d.rectangle([0, 0, w - 1, h - 1], outline=(235, 40, 200), width=4)
    d.rectangle([0, 0, w - 1, 7], fill=(240, 210, 30))  # yellow = TOP edge marker
    try:
        font = ImageFont.truetype("/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf", 26)
    except OSError:
        font = ImageFont.load_default()
    bbox = d.textbbox((0, 0), label, font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    d.text(((w - tw) / 2, (h - th) / 2 - 2), label, fill=(255, 255, 255), font=font)
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


def paint_zones(fd: int, ctrl: str, zw: int, zh: int) -> None:
    # Paint all 4 strip zones (wire 1..4) the same zw x zh square. The zones are
    # discrete (one per knob) with gaps between them by design — they do not tile.
    jpeg = make_target(zw, zh, f"{zw}x{zh}")
    print(f"# control node: {ctrl}")
    print(f"# ZONES: {zw}x{zh} -> all 4 strip zones (wire 1..4).")
    for e, wire in enumerate(ENCODER_WIRE):
        send_image(fd, wire, jpeg)
        print(f"#   E{e + 1} (wire {wire}) <- {zw}x{zh}  ({len(jpeg)} B)")
        time.sleep(0.02)
    print("# done. Photograph the strip.")


def paint_zone_sweep(fd: int, ctrl: str, widths: list[int], h: int) -> None:
    # Paint the 4 zones at 4 candidate widths (height fixed) for one-photo comparison.
    print(f"# control node: {ctrl}")
    print(f"# ZONE-SWEEP: widths {widths} x {h} across the 4 strip zones (wire 1..4).")
    for e, wire in enumerate(ENCODER_WIRE):
        w = widths[e]
        send_image(fd, wire, make_target(w, h, f"{w}x{h}"))
        print(f"#   E{e + 1} (wire {wire}) <- {w}x{h}")
        time.sleep(0.02)
    print("# done. Photograph the strip and compare the 4 widths.")


def paint_key_sweep(fd: int, ctrl: str) -> None:
    # Ascending sizes so one photo reveals which size fills a (uniform) key LCD.
    sizes = [112, 116, 120, 124, 128, 112, 118, 124, 130, 136]
    print(f"# control node: {ctrl}")
    print("# SWEEP: each key painted at a different size (label = px). Photograph it.")
    for k in range(1, 11):
        s = sizes[k - 1]
        send_image(fd, key_logical_to_wire(k), make_target(s, s, str(s)))
        print(f"#   K{k:<2} (wire {key_logical_to_wire(k):>2}) <- {s}x{s}")
        time.sleep(0.02)
    print("# done. Photograph the panel; which size label fills its key edge-to-edge?")


def paint_keys_identical(fd: int, ctrl: str, size: int) -> None:
    # DECISIVE: one buffer, byte-identical, to every key.
    jpeg = make_target(size, size, str(size))
    digest = sum(jpeg) & 0xFFFFFFFF
    print(f"# control node: {ctrl}")
    print(
        f"# DECISIVE: byte-identical {size}x{size} JPEG "
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


def main() -> int:
    ap = argparse.ArgumentParser(description="AKP05E per-key margin diagnostic")
    ap.add_argument("--node", help="control hidraw node (default: auto 0xFFA0)")
    ap.add_argument("--size", type=int, default=KEY_SIZE, help=f"key image px (default {KEY_SIZE})")
    ap.add_argument("--sweep", action="store_true", help="ascending size per key (112,116,...)")
    ap.add_argument(
        "--zones",
        action="store_true",
        help="paint the 4 touch-strip zones (wire 1..4) instead of keys",
    )
    ap.add_argument(
        "--zsweep",
        action="store_true",
        help="paint the 4 zones at 4 candidate widths (--widths x --zh)",
    )
    ap.add_argument(
        "--widths",
        default="184,192,200,208",
        help="4 comma-separated zone widths for --zsweep (default 184,192,200,208)",
    )
    ap.add_argument("--zw", type=int, default=ZONE_W, help=f"zone width px (default {ZONE_W})")
    ap.add_argument("--zh", type=int, default=ZONE_H, help=f"zone height px (default {ZONE_H})")
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
        if args.zsweep:
            widths = [int(x) for x in args.widths.split(",")]
            if len(widths) != 4:
                print("ERROR: --widths needs exactly 4 comma-separated values", file=sys.stderr)
                return 2
            paint_zone_sweep(fd, ctrl, widths, args.zh)
        elif args.zones:
            paint_zones(fd, ctrl, args.zw, args.zh)
        elif args.sweep:
            paint_key_sweep(fd, ctrl)
        else:
            paint_keys_identical(fd, ctrl, args.size)
    finally:
        os.close(fd)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
