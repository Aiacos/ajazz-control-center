# Capture Evidence — sanitised control-channel byte dumps

> These are the **exact wire bytes** observed on real hardware this session
> (2026-05-21) via Frida hooks of the vendor driver and our own hidapi probes.
> **Sanitised:** control-channel feature/output reports only — NO keyboard or
> mouse-coordinate input reports, so no keystrokes/passwords are present. This is
> the safe, committable substitute for raw `.pcap` files (which are rejected by
> policy because they would contain plaintext keystrokes). Use these on Fedora to
> verify the wire format **without re-capturing**; reproduce with the `scripts/`
> probes if needed.

All bytes are hex, in HID-report order (byte 0 = report id where applicable).

---

## AK980 PRO keyboard (0x0c45:0x8009)

### HID collections (Windows `hid.enumerate`)
```
UP=0xff13 usage=0x01 iface=3   MI_03   <-- vendor CONTROL collection (RTC/battery/RGB/TFT)
UP=0xffff usage=0x01 iface=1   MI_01&Col05
UP=0xff68 usage=0x61 iface=2   MI_02
UP=0x0001 usage=0x06 iface=0   MI_00   (boot keyboard — what hid_open picks; wrong for control)
UP=0x0001 usage=0x06 iface=1   MI_01&Col03 (KBD)
UP=0x000c usage=0x01 iface=1   MI_01&Col01 (consumer)
UP=0x0001 usage=0x80 iface=1   MI_01&Col02 (system control)
UP=0x0001 usage=0x02 iface=1   MI_01&Col04 (mouse)
```

### Time-sync (4-packet feature-report envelope; readback with 30 ms settle)
Setting 2026-05-21 11:11:00, with per-packet `sleep(30ms)` + GET_REPORT readback:
```
START     send=65   readback <- 00 04 18 00 00 00 00 00 00 00 00 00
PREAMBLE  send=65   readback <- 00 04 28 00 01 00 00 00 00 01 00 00   (byte4=0x01 device-computed)
DATA      send=65   readback <- 00 00 01 5a 1a 05 15 0b 0b 00 00 04
SAVE      send=65   readback <- 00 04 02 00 00 00 00 00 00 00 00 00
```
DATA decode: `5a`=magic, `1a`=year-2000=26→2026, `05`=May, `15`=21, `0b`=11h, `0b`=11m, `00`=sec, `04`=dow (Thu, POSIX Sun=0). With delay 0 the readback PREAMBLE returns `…28 00 00…` (byte4=0x00, not processed) and the clock does NOT move — the 30 ms settle is required.

### Battery (opcode 0x20 sub 0x01, GET into a 65-byte buffer)
```
batteryPercent GET: n=65  bytes = 00 20 01 00 ff 00 00 00
```
resp[1]=0x20 (echo), resp[4]=0xFF (wired+full → clamp to 100). A 64-byte GET buffer makes `hid_get_feature_report` fail (returns -1).

---

## AJ-series 2.4G 8K mouse (0x3151:0x5007)

### HID collections (Windows `hid.enumerate`)
```
UP=0xffff usage=0x02 iface=2   MI_02          <-- vendor CONTROL collection (clock/battery)
UP=0xffff usage=0x01 iface=1   MI_01&Col04    (NOT control)
UP=0x0001 usage=0x02 iface=0   MI_00          (boot mouse — what hid_open picks; wrong)
UP=0x000c usage=0x01 iface=1   MI_01&Col01    (consumer)
UP=0x0001 usage=0x80 iface=1   MI_01&Col02    (system control)
UP=0x0001 usage=0x06 iface=1   MI_01&Col03 (KBD)
```

### OLED clock (opcode 0x28) — captured from the vendor iot_driver via Frida HidD_SetFeature
Four consecutive vendor sends (seconds incrementing, ~2 s apart):
```
00 28 00 00 00 00 00 00 d7 07 ea 05 15 0b 30 3b ...   (11:48:59)
00 28 00 00 00 00 00 00 d7 07 ea 05 15 0b 31 01 ...   (11:49:01)
00 28 00 00 00 00 00 00 d7 07 ea 05 15 0b 31 03 ...   (11:49:03)
00 28 00 00 00 00 00 00 d7 07 ea 05 15 0b 31 05 ...   (11:49:05)
```
Decode: report-id `00`, opcode `28`, **fixed marker `d7` @ byte 8**, year `07 ea`=2026 (big-endian), `05`=May, `15`=21, `0b`=11h, `30/31`=48/49m, `3b/01/03/05`=sec. NO checksum. Our replay of `00 28 …d7 07ea 05 15 09 09 00` (09:09) made the basetta show 09:09 — confirmed.

The vendor's idle status-poll heartbeat (also via HidD_SetFeature, ~1/s, len 67): `00 f7 00 00 00 …` (all-zero payload). A `00 f1 00 00 00 00 00 00 0e …` was seen once.

### Battery (status report 0x05, GET_FEATURE) — across a wireless replug
```
05 00 00 64 01 01 01 02    <- stable, byte3=0x64=100%
05 00 00 00 00 00 00 00    <- fresh reconnect: link not ready (bytes 4..7 all zero)
05 ad 04 01 00 00 00 00    <- GARBAGE transient frame (byte1/2 != 0) — source of the spurious "1%"
05 00 00 00 01 01 01 02    <- link up, charge not yet reported (byte3=0 → grey)
```
Frame-validity rule: a real frame has **byte1==0 && byte2==0**; charge = byte 3. The `05 ad 04 01` frame is rejected (commit 1f2be0c).

---

## Stream Dock AKP family

No live capture this session (the AKP05 image-render issue is being diagnosed on Fedora). Reference framings from `[uriziel-akp153]`:
```
43 52 54 00 00 42 41 54 00 00 08 7C 0D 00 00 00   <- CRT BAT image header (size 0x087C, key idx 0x0D)
43 52 54 00 00 53 54 50 00 00 00 00 00 00 00 00   <- CRT STP flush after image
device ACK: 41 43 4B 00 00 4F 4B                  <- "ACK\0\0OK"
```
AKP05E `0x0300:0x3004` firmware-version handshake (`CRT VER`) returned the ASCII string `V3.AKP05E.01.007` (2026-05-20). The Fedora capture targets are in `unexplored.md`.

---

## Reproducing on another machine
The probes that produced the mouse/keyboard evidence are in `scripts/` (committed):
```
python3 scripts/ak980_tft_probe.py --enumerate          # keyboard collections
python3 scripts/ak980_tft_probe.py --settime 11:11 --delay 30 --readback
python3 scripts/aj_mouse_probe.py  --enumerate           # mouse collections
python3 scripts/aj_mouse_probe.py  --battery             # battery report 0x05
python3 scripts/aj_mouse_probe.py  --battery-watch 90    # the replug transient
python3 scripts/aj_mouse_probe.py  --clock 09:09         # OLED clock 0x28
```
The Frida capture (`scripts/aj_mouse_frida_capture.py`) only works against the running **Windows** vendor `iot_driver` — there is no vendor driver on Fedora, so on Linux use the direct probes above.
