#!/usr/bin/env python3
"""AJAZZ 2.4G 8K / AJ159 APEX mouse probe — dev-time hardware validation.

Mirrors the wire format reverse-engineered from the vendor Electron driver
(resources/app/main_dist/main_beautified.js): the OLED-clock RTC opcode 0x28
and the battery query 0x82, framed for the 0xFFFF vendor HID collection with a
BIT7 checksum at the last byte.

The report-id VALUE lives inside the native iot_driver.exe (not the JS), so we
try both 0x05 (current C++ kReportId) and 0x00.

NOT a production tool (writes raw HID). Examples:
    python scripts/aj_mouse_probe.py --enumerate
    python scripts/aj_mouse_probe.py --clock 03:33 --report-id 0x05
    python scripts/aj_mouse_probe.py --battery --report-id 0x05
"""

from __future__ import annotations

import argparse
import contextlib
import datetime
import sys
import time

import hid

VID, PID = 0x3151, 0x5007
CTRL_USAGE_PAGE = 0xFFFF
CTRL_USAGE = 0x02  # JS: usage:2, usagePage:65535
REPORT_SIZE = 65  # 1 report-id byte + 64-byte body

FEA_CMD_SET_OLEDCLOCK = 0x28
FEA_CMD_GET_BATTERY = 0x82


def enumerate_collections() -> list[dict]:
    return list(hid.enumerate(VID, PID))


def print_enumeration() -> None:
    for d in enumerate_collections():
        up = d["usage_page"]
        tag = (
            "   <-- 0xFFFF/usage2 control"
            if (up == CTRL_USAGE_PAGE and d["usage"] == CTRL_USAGE)
            else ""
        )
        print(f"UP=0x{up:04x} usage=0x{d['usage']:02x} iface={d['interface_number']}{tag}")
        print("   path=" + d["path"].decode(errors="replace"))


def stamp_bit7(pkt: bytearray) -> None:
    """BIT7 checksum at the last byte = sum(pkt[1..len-2]) & 0x7F (matches the
    C++ stampBit7Checksum: report id excluded, checksum slot excluded)."""
    total = sum(pkt[1 : len(pkt) - 1])
    pkt[len(pkt) - 1] = total & 0x7F


def open_control() -> hid.device:
    path = None
    for d in enumerate_collections():
        if d["usage_page"] == CTRL_USAGE_PAGE and d["usage"] == CTRL_USAGE:
            path = d["path"]
            break
    if path is None:
        sys.exit(f"No 0x{CTRL_USAGE_PAGE:04x}/usage{CTRL_USAGE} collection for {VID:04x}:{PID:04x}")
    dev = hid.device()
    dev.open_path(path)
    return dev


def build_clock(report_id: int, dt: datetime.datetime) -> bytes:
    # JS body (opcode@0) + report-id prepend => our pkt index = body index + 1.
    # Vendor capture (HidD_SetFeature): 00 28 00*6 d7 <yrHi yrLo> M D h m s, NO
    # checksum. The 0xd7 marker at byte 8 is REQUIRED — without it the firmware
    # ignores the packet (that was the bug in the earlier probes).
    pkt = bytearray(REPORT_SIZE)
    pkt[0] = report_id  # 0x00 on the wire
    pkt[1] = FEA_CMD_SET_OLEDCLOCK  # 0x28
    pkt[8] = 0xD7  # fixed marker (byte 8)
    pkt[9] = (dt.year >> 8) & 0xFF  # year big-endian
    pkt[10] = dt.year & 0xFF
    pkt[11] = dt.month
    pkt[12] = dt.day
    pkt[13] = dt.hour
    pkt[14] = dt.minute
    pkt[15] = dt.second
    # NO BIT7 checksum — the vendor sends zeros after the time fields.
    return bytes(pkt)


def set_clock(hhmm: str, report_id: int, use_output: bool, readback: bool) -> None:
    hour, minute = (int(x) for x in hhmm.split(":"))
    today = datetime.date.today()
    dt = datetime.datetime(today.year, today.month, today.day, hour, minute, 0)
    dev = open_control()
    pkt = build_clock(report_id, dt)
    send = dev.write if use_output else dev.send_feature_report
    kind = "output" if use_output else "feature"
    print(f"clock 0x28 -> {hhmm} (report-id 0x{report_id:02x}, {kind} report)")
    print(f"  pkt[0..15]={pkt[:16].hex(' ')}  checksum[64]={pkt[64]:#04x}")
    n = send(pkt)
    print(f"  send -> {n}")
    if readback:
        time.sleep(0.03)
        try:
            r = dev.get_feature_report(report_id, REPORT_SIZE)
            print(f"  readback={bytes(r)[:16].hex(' ')}")
        except OSError as exc:
            print(f"  readback err: {exc}")
    dev.close()
    print("  done — check the dock/basetta TFT clock.")


def query_battery(report_id: int) -> None:
    dev = open_control()
    pkt = bytearray(REPORT_SIZE)
    pkt[0] = report_id
    pkt[1] = FEA_CMD_GET_BATTERY  # 0x82
    stamp_bit7(pkt)
    print(f"battery 0x82 query (report-id 0x{report_id:02x}): {bytes(pkt)[:8].hex(' ')} …")
    n = dev.send_feature_report(bytes(pkt))
    print(f"  send_feature_report -> {n}")
    time.sleep(0.03)
    try:
        r = bytes(dev.get_feature_report(report_id, REPORT_SIZE))
        print(f"  get_feature_report: len={len(r)} {r[:16].hex(' ')}")
        print("  (JS keyboard parse: percent=a[1], state=a[2], lp=a[3] — in the")
        print("   report-id-included layout that maps to resp[2]/resp[3]/resp[4])")
    except OSError as exc:
        print(f"  get_feature_report err: {exc}")
    dev.close()
    print("\nTell me the mouse's ACTUAL battery % so we can locate the byte.")


def watch_battery(seconds: int) -> None:
    """Poll status report 0x05 ~1x/s and dump the full first bytes, so we can
    watch the battery-percent byte (resp[3]) and the flag bytes (resp[4..7])
    settle after a wireless replug — to find a 'data valid' flag that lets us
    suppress the transient low/zero reading."""
    print(f"watching report 0x05 for {seconds}s — unplug/replug (or sleep/wake) the mouse now…")
    print("  t(s)  b0 b1 b2 b3(pct) b4 b5 b6 b7")
    start = time.time()
    last = None
    dev = None
    while time.time() - start < seconds:
        try:
            if dev is None:
                dev = open_control()  # (re)acquire after a replug
            r = bytes(dev.get_feature_report(0x05, REPORT_SIZE))
            row = (
                f"  {time.time() - start:4.0f}  "
                + " ".join(f"{r[i]:02x}" for i in range(8))
                + f"   pct={r[3]}"
            )
        except (OSError, SystemExit):
            if dev is not None:
                with contextlib.suppress(OSError):
                    dev.close()
            dev = None
            row = f"  {time.time() - start:4.0f}  <disconnected / reopening>"
        if row[8:] != (last or "")[8:]:  # print only when the content changes
            print(row, flush=True)
            last = row
        time.sleep(1.0)
    if dev is not None:
        dev.close()
    print("done.")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--enumerate", action="store_true")
    ap.add_argument("--clock", metavar="HH:MM", help="set the OLED clock (opcode 0x28)")
    ap.add_argument("--battery", action="store_true", help="query battery (opcode 0x82)")
    ap.add_argument("--report-id", default="0x05", help="report id byte (try 0x05 or 0x00)")
    ap.add_argument("--output", action="store_true", help="use output report instead of feature")
    ap.add_argument("--readback", action="store_true", help="GET_FEATURE after the clock write")
    ap.add_argument(
        "--battery-watch",
        type=int,
        metavar="SECS",
        default=0,
        help="poll report 0x05 for SECS seconds (watch the replug transient)",
    )
    args = ap.parse_args()
    rid = int(args.report_id, 0)
    if args.battery_watch:
        watch_battery(args.battery_watch)
        return
    if args.clock:
        set_clock(args.clock, rid, args.output, args.readback)
        return
    if args.battery:
        query_battery(rid)
        return
    print_enumeration()


if __name__ == "__main__":
    main()
