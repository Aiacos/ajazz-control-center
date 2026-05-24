"""Confirm the vendor's ~1 Hz 0xF7 status poll populates the wireless mouse
charge into status report 0x05. Per RE (capture-evidence.md): SET_FEATURE,
report-id 0x00, body[0]=0xF7, all-zero payload, ~1/s, on 0xFFFF/usage-0x02.
Then GET_FEATURE report 0x05 — charge at byte 3 (Windows). Try buffer lengths
65 and 67. Move/keep the mouse on while this runs."""

import time

import hid

VID = 0x3151
PID = 0x5007
CTRL_UP = 0xFFFF
CTRL_USAGE = 0x02


def hx(b):
    return " ".join(f"{x:02x}" for x in b[:12])


def main():
    path = None
    for d in hid.enumerate(VID, PID):
        if d["usage_page"] == CTRL_UP and d["usage"] == CTRL_USAGE:
            path = d["path"]
            break
    if not path:
        raise SystemExit("control collection 0xFFFF/usage2 not found")
    dev = hid.device()
    dev.open_path(path)
    print("opened control collection; sending 0xF7 status poll ~1/s...\n")
    try:
        for i in range(20):
            for length in (65, 67):
                poll = bytearray(length)
                poll[0] = 0x00  # report id
                poll[1] = 0xF7  # status poll opcode
                try:
                    dev.send_feature_report(bytes(poll))
                except Exception as exc:
                    print(f"  F7 len={length} send err: {exc}")
                    continue
                time.sleep(0.03)
                try:
                    r = bytes(dev.get_feature_report(0x05, 65))
                except Exception as exc:
                    print(f"  read 0x05 err: {exc}")
                    continue
                charge = r[3] if len(r) > 3 else None
                flag = hx(r[4:8]) if len(r) >= 8 else "?"
                print(f"[{i}] F7 len={length}: {hx(r)}  charge(byte3)={charge} flags={flag}")
            time.sleep(0.9)
    finally:
        dev.close()


if __name__ == "__main__":
    main()
