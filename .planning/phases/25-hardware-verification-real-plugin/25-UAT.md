---
status: testing
phase: 25-hardware-verification-real-plugin
source: [25-01-PLAN.md Task 2; mirrors 10-UAT.md structure; covers VERIFY-05 + VERIFY-06 + provisional-§5 reconciliation]
started: 2026-05-28T08:50:00Z
updated: 2026-05-28T08:50:00Z
---

## Current Test

<!-- OVERWRITE each test - shows where we are -->

number: 1
name: Push image to an LCD key
expected: |
With an AKP05E (0300:3004 firmware-confirmed, OR a retail SKU advertising the
same firmware family) connected, launch the app, select the AKP05E in the
sidebar, and assign/push an image to one of the 10 LCD keys. The image
appears on that physical key.
awaiting: operator

______________________________________________________________________

## Read first (do NOT skip)

This runbook is the load-bearing deliverable of Phase 25 — the **VERIFY-05**
hardware UAT and the **VERIFY-06** real-`.sdPlugin` round-trip against a
physically connected AKP05E. The automated precondition (Phase 14-24 ctest
suite green) must hold before starting. Run:

```
ctest --preset linux-release -E qml
```

The QML-tests link target is a pre-existing latent issue (see CLAUDE.md
"Latent items"); skip it with `-E qml`. The unit suite must be 645/645
passed.

### Hardware preconditions

1. **Build precondition.** Qt6 `CorePrivate` (`qzipreader_p.h`) is REQUIRED at
   configure time. On Fedora: `sudo dnf install qt6-qtbase-private-devel`.
   This is an environment dep, NOT a project-tree mutation.

1. **Device under test.** An AJAZZ AKP05E (USB `0x0300:0x3004`, firmware
   `V3.AKP05E.01.007` per `CRT VER` handshake) OR a retail Mirabox N4 /
   AJAZZ AKP05E with equivalent firmware.

1. **udev / ACL.** `/dev/hidraw*` for the device must have `uaccess` ACL
   granting the current user read+write. Project rule (`resources/linux/70-ajazz.rules`)
   numbered `70-` so it sorts BEFORE `73-seat-late.rules`. If `ls -l /dev/hidraw*`
   shows root-only after install:

   - **Physically replug the device** (per CLAUDE.md, systemd ≥258 does NOT
     restore `uaccess` on `udevadm trigger`; only physical replug or boot does).
   - Dev-only transient: `sudo setfacl -m u:$(id -u):rw /dev/hidraw<N>` (survives
     until next replug).

### Demo-unit caveat (READ before claiming a FAIL)

The lab demo unit at `0x0300:0x3004` (label "HOTSPOTEKUSB HID DEMO") is a
white-label engineering / demo SKU. CLAUDE.md AKP05E glossary documents that
**input streaming (key / encoder / touch) is NOT reachable on this unit** —
proven across hidraw raw read, GET_REPORT polling, evdev, raw usbmon, and the
reference library `4ndv/mirajazz` (its own `async_hid` backend with the exact
`DIS`+`LIG`+`CONNECT` keep-alive). All five captured zero input on press.
Kernel arms EP `0x82` correctly (`S Ii:1:NNN:2 -115:1 512 <` in usbmon); the
device declines to fill it.

**Implication for this UAT:**

- Tests 1 (image on key), 7 (brightness), 8 (clear all), 9 (hasClock=false)
  ARE reachable on the demo unit and have been informally confirmed
  2026-05-28 — but the operator still records them PASS/FAIL formally here.
- Tests 2-6 (input — key press, encoder rotate/press, touch tap, swipe) are
  **expected BLOCKED** on the demo unit. Record `result: BLOCKED — demo unit 0x3004 does not stream input (CLAUDE.md AKP05E glossary §7.1)`. Do NOT
  record FAIL — this is the documented hardware behaviour, not a software
  regression. To convert BLOCKED → PASS on these tests you need either:
  1. A retail AKP05E or Mirabox N4 (different unit), OR
  1. The Frida-on-Windows-vendor-app capture path (decisive method per
     `~/MEGAsync/ajazz-reverse-engineering/dossier/methods-and-tooling.md §2`).

______________________________________________________________________

## Tests

### 1. Push image to an LCD key (VERIFY-05)

expected: After selecting the AKP05E in the sidebar and assigning a JPEG image
(e.g., 85×85 PNG converted via the in-app pipeline) to LCD key 1, the image
appears on that physical key without needing a manual flush. The `BAT`
write+chunks+`ULEND` sequence on hidraw is the path; the live render was
informally confirmed 2026-05-28 on the demo unit. Operator: open the app,
select AKP05E, drag an image onto key 1, observe.
result: [pending]

### 2. Encoder rotate fires actions (VERIFY-05)

expected: Rotating each of the 4 endless rotary encoders clockwise and
counter-clockwise fires a `dialRotate` event (positive delta on CW, negative
on CCW). Per the akp05.md provisional §5 framing the encoder index is 0..3
and the delta is ±1 per tick. Bound to a built-in action (e.g., volume up/down)
on Plan 21-03 the operator hears a system-volume change on each tick.
result: [pending] *(BLOCKED on 0x3004 demo unit — see "Demo-unit caveat".)*

### 3. Encoder press fires action (VERIFY-05)

expected: Pressing each of the 4 rotary encoders fires a `keyDown` (per the
press-only-then-synthesised-release pattern that the AKP05 backend emits —
see commit `670348e`, `WR-05 clarify EncoderReleased dispatch`). On press a
bound action (e.g., toggle mute) triggers; on synthesised release the action
does NOT re-trigger.
result: [pending] *(BLOCKED on 0x3004 demo unit.)*

### 4. Touch-strip tap fires per-zone action (VERIFY-05)

expected: Tapping each of the 4 touch-strip zones (under encoder 1..4) fires
a touch event with the correct zone index (0..3). Per the provisional §5
table (`akp05.md` lines 125-129) gesture code `0x0` = Tap, payload bytes
10..11 = zone index. The app routes the touch to the action bound to the
under-encoder action slot.
result: [pending] *(BLOCKED on 0x3004 demo unit. Note: this test also
DOUBLES as the touch-zone-map confirm-or-correct row — see Test 10.)*

### 5. Touch-strip swipe changes page (VERIFY-05)

expected: A left-to-right swipe across the touch strip advances to the next
ProfilePage; a right-to-left swipe goes to the previous page. Per the
provisional §5 table gesture codes `0x1` (swipe-left) and `0x2` (swipe-right)
carry the start-X in bytes 10..11 BE16. The app's `pageNavigated`
signal (per `805bce3 feat(16-03): wire pageNavRequested -> navigatePage`)
should fire.
result: [pending] *(BLOCKED on 0x3004 demo unit. Test also doubles as the
swipe-gesture-code confirm-or-correct row — see Test 11.)*

### 6. Push image to a touch-strip zone (VERIFY-05)

expected: Assigning an image to encoder slot 1's touch-strip zone makes that
zone show the image on the touch strip. The current `assignEncoderImage` path
(commit `27c04db feat(23-01): add assignMainImage/assignEncoderImage/assignTouchStripZone`)
sends a `BAT` write with the touch-strip zone key-byte mapping (enc 1..4 →
strip 5 → bottom 6..10 → top 11..15 per CLAUDE.md AKP05E glossary). The image
appears on the correct touch-strip zone (NOT on an LCD key).
result: [pending]

### 7. Global brightness slider works (VERIFY-05)

expected: Moving the brightness slider in the device's Keys tab visibly
changes the AKP05E's overall LCD brightness from min to max. The `LIG` opcode

- percentage byte path (`StreamDockControlService::setBrightness`) was
  informally confirmed live 2026-05-28. Operator: drag slider 0% → 100%,
  observe.
  result: [pending]

### 8. Clear all blanks the panel (VERIFY-05)

expected: The "Clear all" button blanks every LCD key + touch strip on the
AKP05E (CLE opcode). Informally confirmed 2026-05-28.
result: [pending]

### 9. hasClock=false honesty (no Sync button) (VERIFY-05)

expected: The AKP05E sidebar row does NOT show a Sync button / "Time
synchronization" section. `DeviceDescriptor::hasClock = false` is set on the
akp05e row at `src/devices/streamdeck/src/register.cpp:309` per DEVICES-11 /
ARCH-05 (Stream Dock family has no firmware RTC; landed via commit `07c5902`).
The UI is gated by `visible: root.hasClock` in `SettingsRow.qml:196`.
Operator: select AKP05E, open `Settings` tab in `ProfileEditor`, confirm
absence.
result: [pending]

______________________________________________________________________

## Provisional §5 confirm-or-correct (RE doc reconciliation)

These rows reconcile the **provisional** wire-protocol values in
`docs/protocols/streamdeck/akp05.md` §5 (currently marked "Unverified from
a real capture" — lines 99-103) and the related `DRA` row in
`akp05_vendor.md` §2 (Ghidra-decompiled but the rect interpretation needs
hardware corroboration). The operator runs the matching VERIFY-05 test and
records whether the documented value held or was contradicted by the device.

**Hardware wins.** On any mismatch the operator updates the matching RE doc
in the same session (per CLAUDE.md methodology rule: "When the RE and the
hardware disagree, the hardware wins — and update the RE doc.").

### 10. Touch-strip zone+gesture map (akp05.md lines 115-130)

expected_value: From `akp05.md` §"Tag byte at offset 9": tag range `0x30..0x3F`
is touch-strip gestures with **lower nibble = gesture code**. Code `0x0` =
Tap with payload bytes 10..11 = zone index 0..3. Codes `0x1`/`0x2` =
swipe-left/right with payload bytes 10..11 = start-X (BE16, 0..639). Tap zones
align with encoder positions (zone 0 under encoder 1, ... zone 3 under
encoder 4).

confirm_or_correct: [pending — driven by Tests 4 + 5]

how_to_correct: If the device returns a gesture code OR zone index OR
swipe-X-payload encoding that disagrees with the table above, update
`docs/protocols/streamdeck/akp05.md` "Tag byte at offset 9" section + the
"Suggested gesture-code mapping" table inline, removing the "to verify" note
and citing this UAT session. If the framing is completely different (e.g.,
the tag range is NOT `0x30..0x3F`), keep the old text but ADD a
hardware-corroborated section above with the verbatim observed bytes —
preserves audit trail.

### 11. DRA rect header layout (akp05_vendor.md §2, line 193)

expected_value: `DRA` opcode (touch-strip rect-addressable image update) has
the 32-byte header packet: bytes 5..7 = `D R A`; bytes 8..11 = BE32
`size+0x20`; byte 12 = `location`; bytes 13..14 = BE16 `rect.width`; 15..16 =
BE16 `rect.height`; 17..18 = BE16 `rect.x`; 19..20 = BE16 `rect.y`. Then JPEG
data follows.

confirm_or_correct: \[pending — driven by Test 6 if/when `DRA` is wired into
the app's encoder-image path; currently the app uses `BAT` per the AKP05E
glossary, so `DRA` may be a future-work opcode\]

how_to_correct: If the rect field order is NOT (width, height, x, y) BE16, or
the location byte is at a different offset, update `akp05_vendor.md` §2 line
193 (the `DRA` row in the opcode table). Note: the Ghidra decompile is
authoritative for byte offsets that the SDK fills out (host→dev direction),
so a contradiction here would be a misread of Ghidra rather than a wire
truth.

### 12. ENC vs MAI vs BAT mapping for encoder-area image (akp05.md lines 132-145)

expected_value: From `akp05.md` §"Output reports (host → device)": `ENC` =
encoder LCD area in the strip (size + 0-based encoder index); `MAI` =
full-width touch strip (size only, target implicit); `BAT` = key image (size

- 1-based key index). The current code uses `BAT` for both LCD keys AND
  touch-strip zones (key-byte-mapping covers both — enc 1..4 = touch strip
  zones, 6..15 = LCD keys per CLAUDE.md glossary). So `ENC`/`MAI` may be unused
  in the live code paths but DOCUMENTED in `akp05.md`.

confirm_or_correct: [pending — driven by Test 6 (touch-strip zone image)]

how_to_correct: If Test 6 confirms `BAT` is the correct opcode for
touch-strip zones (which is what the in-code path uses), update `akp05.md`
to mark `ENC` and `MAI` as "vendor opcodes recognised by the device but NOT
used by this control center — `BAT` with the zone-mapped key-byte is the
unified path" with a pointer to commit `037bd8d` (akp05KeyWire). If `BAT`
does NOT work and the device requires `ENC` for the encoder area, that is a
P1 code bug — file under `akp05KeyWire` regression.

______________________________________________________________________

## VERIFY-06 — Real third-party `.sdPlugin` round-trip

expected: A real third-party Elgato-compatible or Mirabox-compatible
`.sdPlugin` (e.g., the bundled built-in Counter or a published community
plugin) registers over the loopback WebSocket (`SdPluginServer`), paints a
key via `setImage`, and receives `keyDown` / `dialRotate` from a physical
press/turn with its observable effect (per `4f6a212 feat(19-02): implement PluginDeviceBridge inbound action router + onSetImage` + `f95c721 feat(19-03): wire outbound bridge in Application + loopback e2e tests`).

### 13. Plugin handshake over loopback WebSocket (VERIFY-06)

expected: Launch a sample `.sdPlugin` (one of the bundled built-ins under
`src/plugins/built-in/` OR a community plugin installed via PluginStore).
Confirm in the app log: the plugin's `register` message arrives over the
loopback WebSocket; the `passHello` auth handshake (commit `d9fd224 feat(17-03)`)
completes; the plugin appears in `LoadedPluginsPage` (the QML view).
result: [pending] *(NOT blocked by demo unit — handshake is loopback, not
HID. This test should pass on the demo unit.)*

### 14. Plugin `setImage` paints a key (VERIFY-06)

expected: The plugin issues a `setImage` action targeting an action context
on the AKP05E. The `PluginDeviceBridge::onSetImage` path
(`src/app/src/plugin_device_bridge.cpp`) decodes the data URI and routes the
image through the device's `assignMainImage` (per Plan 23-01). The image
appears on the targeted LCD key.
result: [pending] *(Reachable on the demo unit — image render works.)*

### 15. Plugin receives `keyDown` from physical press (VERIFY-06)

expected: With the plugin loaded and an action bound to an LCD key on the
AKP05E, a physical press fires a `keyDown` event over the WebSocket. The
plugin's handler runs; an observable effect (e.g., the plugin updates the
key image or fires a system action) occurs.
result: [pending] *(BLOCKED on 0x3004 demo unit — input streaming
unreachable. Use a retail AKP05E / Mirabox N4 or the Frida-on-Windows path.)*

### 16. Plugin receives `dialRotate` from physical encoder turn (VERIFY-06)

expected: With the plugin loaded and an action bound to an encoder on the
AKP05E, a physical rotate (CW or CCW) fires a `dialRotate` event over the
WebSocket. The plugin's handler runs; an observable effect occurs.
result: [pending] *(BLOCKED on 0x3004 demo unit.)*

______________________________________________________________________

## Summary

total: 16
passed: 0
issues: 0
pending: 16
skipped: 0

Per-criterion result table (operator: fill as you go):

| #   | Name                                  | Result  | Notes                                 |
| --- | ------------------------------------- | ------- | ------------------------------------- |
| 1   | Push image to an LCD key              | pending |                                       |
| 2   | Encoder rotate fires actions          | pending | BLOCKED on 0x3004                     |
| 3   | Encoder press fires action            | pending | BLOCKED on 0x3004                     |
| 4   | Touch-strip tap fires per-zone action | pending | BLOCKED on 0x3004; doubles as Test 10 |
| 5   | Touch-strip swipe changes page        | pending | BLOCKED on 0x3004; doubles as Test 11 |
| 6   | Push image to a touch-strip zone      | pending |                                       |
| 7   | Global brightness slider works        | pending |                                       |
| 8   | Clear all blanks the panel            | pending |                                       |
| 9   | hasClock=false honesty                | pending |                                       |
| 10  | Touch-strip zone+gesture map (§5)     | pending | driven by Tests 4 + 5                 |
| 11  | DRA rect header layout                | pending | driven by Test 6 if/when DRA is wired |
| 12  | ENC vs MAI vs BAT mapping             | pending | driven by Test 6                      |
| 13  | Plugin handshake (loopback)           | pending |                                       |
| 14  | Plugin setImage paints a key          | pending |                                       |
| 15  | Plugin receives keyDown               | pending | BLOCKED on 0x3004                     |
| 16  | Plugin receives dialRotate            | pending | BLOCKED on 0x3004                     |

Operator name / session: \_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_

## Gaps

\[none yet — the demo-unit input-unreachable items are NOT gaps; they are
documented hardware behaviour. Convert them PASS only with a different unit.\]
