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
    python3 scripts/akp05_key_margin_probe.py --zones         # 4 strip zones at 176x112
    python3 scripts/akp05_key_margin_probe.py --zones --zw 160 --zh 112   # iterate zone size
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
ZONE_W, ZONE_H = 176, 112  # opendeck-akp05 mappings.rs:174 candidate (code currently 128x128)


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
    # Touch-strip zones (wire 1..4). The strip is ONE wide LCD; the firmware places
    # each zone's image at a fixed slot, so an image too small leaves a gap and too
    # large bleeds into the neighbour zone (doc 2026-05-31: "200px overflowed into
    # the neighbour"). Paint all 4 zones the SAME w x h and read fill-vs-gap-vs-bleed.
    # Default 176x112 = opendeck-akp05 mappings.rs:174; re-run with --zw/--zh.
    jpeg = make_target(zw, zh, f"{zw}x{zh}")
    print(f"# control node: {ctrl}")
    print(
        f"# ZONES: {zw}x{zh} (opendeck mappings.rs:174 = 176x112; code 128x128) "
        f"-> all 4 strip zones (wire 1..4)."
    )
    for e, wire in enumerate(ENCODER_WIRE):
        send_image(fd, wire, jpeg)
        print(f"#   E{e + 1} (wire {wire}) <- {zw}x{zh}  ({len(jpeg)} B)")
        time.sleep(0.02)
    print("# done. Photograph the strip; per zone: does the MAGENTA border touch all 4")
    print("#       edges with NO gap and NO bleed into the next zone?")
    print("#       gap => smaller; bleed => larger; clean fill => that's the size.")


def paint_zone_sweep(fd: int, ctrl: str, widths: list[int], h: int) -> None:
    # One photo, 4 candidate zone WIDTHS (height fixed). The strip is one wide LCD
    # with 4 fixed zone slots; flush-boundary method: the boundary between zone n
    # (width w_n) and zone n+1 is gap-free exactly when w_n == the firmware's zone
    # pitch. So the zone whose RIGHT edge meets the next zone's left edge with no
    # gap and no overlap pins the pitch. 128 left gaps; opendeck says 176 wide.
    print(f"# control node: {ctrl}")
    print(f"# ZONE-SWEEP: widths {widths} x {h} across the 4 strip zones (wire 1..4).")
    for e, wire in enumerate(ENCODER_WIRE):
        w = widths[e]
        send_image(fd, wire, make_target(w, h, f"{w}x{h}"))
        print(f"#   E{e + 1} (wire {wire}) <- {w}x{h}")
        time.sleep(0.02)
    print("# done. Photograph the strip; the boundary E_n|E_n+1 that is flush (no gap,")
    print("#       no overlap) means w_n == the zone pitch -> that is the fill width.")


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
