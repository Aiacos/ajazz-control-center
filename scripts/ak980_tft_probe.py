#!/usr/bin/env python3
"""AK980 PRO TFT upload probe — dev-time hardware validation.

Mirrors the C++ chunked-TFT wire format in
src/devices/keyboard/src/proprietary_keyboard.cpp byte-for-byte so we can test
whether the decompile-derived format actually drives the 240x135 panel, and
which transport (output vs feature report) the firmware accepts.

NOT a production tool. Run with the device connected:
    python scripts/ak980_tft_probe.py --enumerate
    python scripts/ak980_tft_probe.py --upload [--feature] [--bars]
"""
from __future__ import annotations

import argparse
import datetime
import sys
import time

import hid

VID, PID = 0x0C45, 0x8009
CTRL_USAGE_PAGE = 0xFF13  # vendor control collection (memory: ak980_hid_interface_selection)

W, H = 240, 135
FRAME_BYTES = W * H * 2
CHUNK_PAYLOAD = 28
REPORT_SIZE = 64
CMD_SCREEN_HEADER = 0x7F
CMD_SCREEN_SUB_BEGIN = 0x03


def enumerate_collections() -> list[dict]:
    return list(hid.enumerate(VID, PID))


def print_enumeration() -> None:
    for d in enumerate_collections():
        up = d["usage_page"]
        print(
            f"UP=0x{up:04x} usage=0x{d['usage']:02x} iface={d['interface_number']} "
            f"in={d.get('input_report_length')} out={d.get('output_report_length')} "
            f"feat={d.get('feature_report_length')}"
            + ("   <-- 0xFF13 control" if up == CTRL_USAGE_PAGE else "")
        )
        print("   path=" + d["path"].decode(errors="replace"))


def stamp_checksum(pkt: bytearray) -> None:
    """Byte-32 checksum = sum(bytes, checksum slot excluded) mod 256."""
    total = sum(pkt[i] for i in range(len(pkt)) if i != 32)
    pkt[32] = total & 0xFF


def build_header(lcd_select: int, total_chunks: int) -> bytearray:
    pkt = bytearray(REPORT_SIZE)
    pkt[0] = 0x00
    pkt[1] = CMD_SCREEN_HEADER
    pkt[2] = CMD_SCREEN_SUB_BEGIN
    pkt[3] = 0x00
    pkt[4] = (lcd_select + 1) & 0xFF
    pkt[5] = total_chunks & 0xFF
    pkt[6] = (total_chunks >> 8) & 0xFF
    pkt[7] = (total_chunks >> 16) & 0xFF
    stamp_checksum(pkt)
    return pkt


def build_chunk(idx: int, payload: bytes) -> bytearray:
    pkt = bytearray(REPORT_SIZE)
    pkt[0] = 0x00
    pkt[1] = 0x80 | ((idx >> 16) & 0x7F)
    pkt[2] = idx & 0xFF
    pkt[3] = (idx >> 8) & 0xFF
    pkt[4 : 4 + len(payload)] = payload
    stamp_checksum(pkt)
    return pkt


def encode_rgb565_solid(r: int, g: int, b: int, bars: bool) -> bytes:
    """240x135 BE RGB565. If bars: top-left 3 px = R,G,B markers (byte-order probe)."""
    out = bytearray(FRAME_BYTES)
    val = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    hi, lo = (val >> 8) & 0xFF, val & 0xFF
    for i in range(0, FRAME_BYTES, 2):
        out[i] = hi
        out[i + 1] = lo
    if bars:
        # pixel (0,0)=red 0xF800, (1,0)=green 0x07E0, (2,0)=blue 0x001F
        for px, color in enumerate((0xF800, 0x07E0, 0x001F)):
            out[px * 2] = (color >> 8) & 0xFF
            out[px * 2 + 1] = color & 0xFF
    return bytes(out)


def open_control(usage_page: int = CTRL_USAGE_PAGE) -> hid.device:
    path = None
    for d in enumerate_collections():
        if d["usage_page"] == usage_page:
            path = d["path"]
            break
    if path is None:
        sys.exit(f"No 0x{usage_page:04x} collection found for {VID:04x}:{PID:04x}")
    dev = hid.device()
    dev.open_path(path)
    return dev


def upload(use_feature: bool, bars: bool, color: tuple[int, int, int]) -> None:
    pixels = encode_rgb565_solid(*color, bars=bars)
    total_chunks = (len(pixels) + CHUNK_PAYLOAD - 1) // CHUNK_PAYLOAD
    dev = open_control()
    send = dev.send_feature_report if use_feature else dev.write
    kind = "feature report" if use_feature else "output report"
    print(f"Uploading test frame via {kind}: {total_chunks} chunks "
          f"(color={color}, bars={bars})")
    n = send(bytes(build_header(0, total_chunks)))
    print(f"  header write returned {n}")
    for i in range(total_chunks):
        off = i * CHUNK_PAYLOAD
        payload = pixels[off : off + CHUNK_PAYLOAD]
        send(bytes(build_chunk(i, payload)))
        if i % 256 == 0:
            time.sleep(0.002)
    dev.close()
    print("  done — check the TFT panel.")


TIME_REPORT_SIZE = 65


def build_time_start() -> bytes:
    pkt = bytearray(TIME_REPORT_SIZE)
    pkt[1] = 0x04
    pkt[2] = 0x18  # CmdStartTime
    return bytes(pkt)


def build_time_preamble() -> bytes:
    pkt = bytearray(TIME_REPORT_SIZE)
    pkt[1] = 0x04
    pkt[2] = 0x28  # CmdSetTime
    pkt[9] = 0x01  # configure-mode marker
    return bytes(pkt)


def build_time_data(dt: datetime.datetime) -> bytes:
    pkt = bytearray(TIME_REPORT_SIZE)
    pkt[0] = 0x00  # HID report id
    pkt[1] = 0x00
    pkt[2] = 0x01  # LCD-select + 1
    pkt[3] = 0x5A  # magic
    pkt[4] = (dt.year - 2000) & 0xFF if dt.year >= 2000 else 0
    pkt[5] = dt.month
    pkt[6] = dt.day
    pkt[7] = dt.hour
    pkt[8] = dt.minute
    pkt[9] = dt.second
    pkt[10] = 0x00
    pkt[11] = (dt.weekday() + 1) % 7  # Python Mon=0 -> POSIX tm_wday Sun=0
    pkt[63] = 0xAA
    pkt[64] = 0x55
    return bytes(pkt)


def build_time_save() -> bytes:
    pkt = bytearray(TIME_REPORT_SIZE)
    pkt[1] = 0x04
    pkt[2] = 0x02  # CmdSaveRtc
    return bytes(pkt)


def set_time(
    hhmm: str,
    usage_page: int = CTRL_USAGE_PAGE,
    use_feature: bool = True,
    delay_ms: int = 0,
    readback: bool = False,
) -> bool:
    """Mirror ProprietaryKeyboard::setTime — 4 packets on the given collection.

    With ``readback`` we also mirror FUN_0044eed0's per-packet GET_FEATURE
    (request/response handshake) and the inter-packet Sleep via ``delay_ms``.
    """
    hour, minute = (int(x) for x in hhmm.split(":"))
    today = datetime.date.today()
    dt = datetime.datetime(today.year, today.month, today.day, hour, minute, 0)
    try:
        dev = open_control(usage_page)
    except SystemExit as exc:
        print(f"  open 0x{usage_page:04x} FAILED: {exc}")
        return False
    send = dev.send_feature_report if use_feature else dev.write
    ok = True
    try:
        for label, pkt in (
            ("START", build_time_start()),
            ("PREAMBLE", build_time_preamble()),
            ("DATA", build_time_data(dt)),
            ("SAVE", build_time_save()),
        ):
            if delay_ms:
                time.sleep(delay_ms / 1000.0)
            n = send(pkt)
            if n is None or n < 0:
                ok = False
            resp = ""
            if readback:
                try:
                    data = dev.get_feature_report(0x00, TIME_REPORT_SIZE)
                    resp = "  <- " + bytes(data[:12]).hex(" ")
                except OSError as exc:
                    resp = f"  <- readback err: {exc}"
            print(f"  {label:9s} -> {n}{resp}")
    except OSError as exc:
        print(f"  write FAILED: {exc}")
        ok = False
    dev.close()
    return ok


SWEEP_COMBOS = [
    (0xFF13, True, "01:01"),
    (0xFF13, False, "02:02"),
    (0xFFFF, True, "03:03"),
    (0xFFFF, False, "04:04"),
    (0xFF68, True, "05:05"),
    (0xFF68, False, "06:06"),
]


def sweep_time(pause: float) -> None:
    print("Sweeping (interface × transport). Watch the panel — note every time that appears.\n")
    for up, feat, hhmm in SWEEP_COMBOS:
        kind = "feature" if feat else "output "
        print(f"[{hhmm}] 0x{up:04x} {kind} report:")
        set_time(hhmm, up, feat)
        time.sleep(pause)
    print("\nSweep done. Which time(s) showed up, and what is on the panel now?")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--enumerate", action="store_true", help="list HID collections")
    ap.add_argument("--upload", action="store_true", help="push a test frame")
    ap.add_argument("--feature", action="store_true", help="use feature reports instead of output")
    ap.add_argument("--bars", action="store_true", help="R/G/B markers at pixels (0,0)/(1,0)/(2,0)")
    ap.add_argument("--color", default="0,0,255", help="solid fill 'R,G,B' (default blue)")
    ap.add_argument("--settime", metavar="HH:MM", help="set the device RTC clock (e.g. 11:11)")
    ap.add_argument("--sweep", action="store_true", help="try every interface × transport combo")
    ap.add_argument("--pause", type=float, default=4.0, help="seconds between sweep combos")
    ap.add_argument("--delay", type=int, default=0, help="ms between time packets")
    ap.add_argument("--readback", action="store_true", help="GET_FEATURE after each packet")
    ap.add_argument("--output", action="store_true", help="use output reports instead of feature")
    args = ap.parse_args()
    if args.sweep:
        sweep_time(args.pause)
        return
    if args.settime:
        print(f"Setting AK980 PRO clock to {args.settime} "
              f"(delay={args.delay}ms readback={args.readback} output={args.output})")
        set_time(args.settime, use_feature=not args.output,
                 delay_ms=args.delay, readback=args.readback)
        return
    if args.enumerate or not args.upload:
        print_enumeration()
    if args.upload:
        color = tuple(int(x) for x in args.color.split(","))
        upload(args.feature, args.bars, color)  # type: ignore[arg-type]


if __name__ == "__main__":
    main()
