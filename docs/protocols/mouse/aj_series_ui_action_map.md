# AJ-series mouse — UI action ↔ opcode map

> Operational map of "what the vendor app's UI does when the user clicks
> a button". Use this when designing the equivalent QML pane in our
> Control Center so the on-device behaviour matches user expectations.
>
> Every entry links a UI action → the JS function it fires → the
> `FEA_CMD_*` opcode that goes on the wire → the `CheckSumType` mode.
> Line numbers refer to `extracted/app32/resources/app/dist/static/js/main_beautified.js`
> (the beautified renderer bundle).

---

## 1 — Renderer state model

The vendor app uses MobX as state container. The "current device" is
held in `deviceStore.currentDev` (a `Mouse` / `Keyboard` instance);
the "current profile" is `deviceStore.currentProfile` (0..7). Most UI
handlers route through a thin wrapper class (the `BaseDevice`-like
super of every concrete `Mouse` / `Keyboard`) that:

1. `checkDev()` — bail out early if no device.
2. Optimistic local-state update via `kr(() => …)` (MobX action wrapper).
3. `doAsync(this.currentDev.setX, callback, args)` — fires the HID
   write; the callback patches local state if the device acked.

For mouse-class devices, almost every setter funnels into one of
**three opcodes**: `0x53` (omnibus), `0x54` (DPI), or `0x07` (LED).

---

## 2 — UI-action × opcode matrix

### 2.1 Sensor settings tab

| UI control | Renderer handler line | Field touched | Final opcode | Checksum |
|------------|------------------------|---------------|-------------:|----------|
| **DPI table edit** (drag any stage's slider, change DPI value, change colour) | `js:1037531`, `js:1037555` (`setMouseOption1({…})`) | `dpiArr[i]` / `DPIColorArr[i]` | `0x54` | BIT7 |
| **Activate DPI stage N** (radio button click) | `js:1037555` (same `setMouseOption1` with new `currentDpi`) | `currentDpi` | `0x54` | BIT7 |
| **DPI count up/down** (change number of enabled stages) | `js:1037555` (`currentDPIMax`) | `currentDPIMax` | `0x54` | BIT7 |
| **Polling rate radio** (125 / 250 / ... / 8000 Hz) | `js:971588`, `js:979338` (`setReportRate`) | `mp.rate` | `0x53` (mouse) or `0x04` (keyboard) | BIT7 |
| **Sensitivity X slider** | `js:1035710` (`setMouseOption0({mp: {xSensitivity:…}})`) | `mp.xSensitivity` (byte 50) | `0x53` | BIT7 |
| **Sensitivity Y slider** | `js:1035721` | `mp.ySensitivity` (byte 51) | `0x53` | BIT7 |
| **Lift-off distance "1 mm" radio** | `js:1035792` (`liftCutOff = 0`) | `mp.liftCutOff` (byte 52) | `0x53` | BIT7 |
| **LOD "2 mm" radio** | `js:1035804` (`liftCutOff = 1`) | `mp.liftCutOff` (byte 52) | `0x53` | BIT7 |
| **LOD "3 mm" radio** | `js:1035816` (`liftCutOff = 2`) | `mp.liftCutOff` (byte 52) | `0x53` | BIT7 |
| **"直线修正" (Angle Snap) toggle** | `js:1035834` (`angelSnap = !angelSnap`) | `mp.angelSnap` (byte 53) | `0x53` | BIT7 |
| **"波纹修正" (Motion Smoothing) toggle** | `js:1035846` (`smooth = !smooth`) | `mp.smooth` (flag bit 2) | `0x53` | BIT7 |
| **Debounce slider** (0..10 ms) | `js:979124` (`setMouseOption0({deBounce: t})`) | `deBounce` (byte 10) | `0x53` | BIT7 |
| **Sleep time editor** (BT idle / BT deep / 2.4G idle / 2.4G deep) | `js:979266` (`setMouseOption0({sleep: t})`) | bytes 40..47 (8-byte block) | `0x53` | BIT7 |

### 2.2 Buttons tab

| UI control | Renderer handler line | Field touched | Final opcode | Checksum |
|------------|------------------------|---------------|-------------:|----------|
| **Single-button rebind** (right-click on button → "Assign Key") | `js:981295`, `js:1050722`, `js:1060837` (`setKeyConfigSimple(config, …)`) | per-button `changeArr` | `0x50` | BIT7 |
| **Fn-layer rebind** | `js:1050792`, `js:1063198` (`setKeyConfigSimple(c, profile+u, fnType, false)`) | per-button `changeArr` on Fn layer | `0x51` | BIT7 |
| **Button-to-wheel mapping** | `js:1035741` (`buttonChange`), `js:1035752` (`wheelToButton`), `js:1035763` (`buttonToWheel`) | bytes 14/15/16 | `0x53` | BIT7 |
| **Macro picker on a button** | (rebind → macro variant: `n[0]=9, n[1]=mode, n[2]=macroIdx`) | per-button `changeArr` | `0x50` | BIT7 |

### 2.3 Macros tab

| UI control | Renderer handler line | Final opcode | Checksum |
|------------|------------------------|-------------:|----------|
| **Save macro** (after editing event list) | `js:980894`, `js:728183` (`setMacro(macroObj, slotIdx)` → chunked `_setMacro` calls) | `0x16` (5 chunks of 64 bytes per macro) | BIT7 per chunk |
| **Read macro back** | (called on profile load: `js:921626` `getMacro(slot)`) | `0x96` (4 reads of 64) | BIT7 |

### 2.4 Lighting tab (mouse with RGB; AJ159 has no per-key RGB but still uses these for battery LED)

| UI control | Renderer handler line | Final opcode | Checksum |
|------------|------------------------|-------------:|----------|
| **Effect picker** (LightOff/AlwaysOn/Breath/Wave/Neon/...) | `js:980049`, `js:980282`, `js:1018444`, `js:1019525`, `js:1024540` (`setLightSetting(setting)`) | `0x07` (8-byte block) | BIT7 |
| **Colour picker (RGB)** | (writes go through the same `setLightSetting`) | `0x07` | BIT7 |
| **Brightness slider** | (writes go through the same `setLightSetting` — brightness IS the `value` field) | `0x07` | BIT7 |
| **Speed slider** | (same — `speed` field) | `0x07` | BIT7 |
| **Dazzle toggle** | (same — `dazzle` field flips mode-bits NORMAL ↔ DAZZLE) | `0x07` | BIT7 |
| **"Light off" master toggle** | `js:979420` (`mouseParam.lightOff = !openBtn` then `setMouseOption0({mp: …})`) | `0x53` (flag bit 0) | BIT7 |
| **"Wheel light off" toggle** | (writes `mp.wheelLightOff` via same `setMouseOption0`) | `0x53` (flag bit 1) | BIT7 |
| **Battery-LED colour high-charge** | `js:1036124` (`c[r] = t, setMouseOption0({mp})`) | `0x53` (bytes 54..56) | BIT7 |
| **Battery-LED colour low-charge** | `js:1036124` | `0x53` (bytes 57..59) | BIT7 |
| **Battery-LED "on while charging" toggle** | `js:1036142` (`setMouseOption0({mp})` with `chargingSwitch`) | `0x53` (byte 60) | BIT7 |
| **Logo RGB (mice with separate logo LED)** | (writes through `LogoLightToBuffer` into bytes 32..39) | `0x53` | BIT7 |

### 2.5 Profile / system tab

| UI control | Renderer handler line | Final opcode | Checksum |
|------------|------------------------|-------------:|----------|
| **Profile selector dropdown** | `js:1050243`, `js:1050261`, `js:1050700`, `js:1062639`, `js:1063084` (`setCurrentProfile(idx)`) | `0x05` | BIT7 |
| **Factory reset / "Restore defaults" button** | (calls `FEA_CMD_SET_RESERT = 0x02`) | `0x02` | BIT7 |
| **About dialog → Firmware version** | `js:970959` (`getFirmwareVersion()`) | `0x80` (read response uint16-LE at bytes 1..2) | BIT7 |
| **OS auto-detect toggle (Mac/Windows)** | (uses `0x17` `SET_AUTOOS_EN`) | `0x17` | BIT7 |

### 2.6 Firmware OTA pane

| UI control | Renderer handler line | Final opcode | Checksum |
|------------|------------------------|-------------:|----------|
| **"Update mouse firmware"** | `js:817368` (mouse-MCU `mledUpgrade()`) | `0x40` (enter boot), data chunks, `0xc1` (checksum verify) | BIT7 per packet |
| **"Update screen firmware"** | `js:817237` (screen-MCU `oledUpgrade()`) | `0x30` (enter boot), data chunks, `0x31` (start) | BIT7 per packet |

### 2.7 Screen / LCD pane

| UI control | Renderer handler line | Final opcode | Checksum |
|------------|------------------------|-------------:|----------|
| **Upload wallpaper image** | `js:817196` (`writeFeatureCmd` with `0x25` for 16-bit RGB565) | `0x25` | BIT7 per packet |
| **Upload 24-bit colour image** | (`0x29` `SET_SCREEN_24BITDATA`) | `0x29` | BIT7 |
| **Upload GIF animation** | `js:817098` (`setUserGifStart` then chunked `setUserGif`) | `0x18` + `0x19` | BIT7 |
| **Set widget = clock / weather / CPU info / custom** | (sled-only — UI sends widget config to `screen` table; no HID write needed because the device polls the host via the gRPC `watchSystemInfo` stream) | (none) | (none) |
| **Clock format / 12h vs 24h** | `0x28` `SET_OLEDCLOCK` | `0x28` | BIT7 |
| **Weather city configuration** | (sled-only; iot_driver's `getWeather` call fetches per `WeatherReq.address`) | (none) | (none) |

### 2.8 Anti-features (NOT to implement)

| UI control | Renderer handler line | Final opcode |
|------------|------------------------|-------------:|
| **Recoil-control pattern editor** | (writes to `gun` sled table + sends `0x60` chunked) | `0x60` — DO NOT IMPLEMENT |
| **Rapid-fire / down-count** | (writes `0x55`) | `0x55` — DO NOT IMPLEMENT |
| **Cloud login → "Sign in"** | `js:53299–53302` calls `axios.post('https://api.rongyuan.tech:3814/v1/login', …)` | (none — HTTPS to vendor cloud) |
| **Cloud share download** | `axios.get('.../share/image/<uuid>')` → cache into `db_img_share` | (none — HTTPS to vendor cloud) |
| **Weather widget** | `js:56760` `fo(lang, addr)` → gRPC `getWeather` → iot_driver fetches `http://w2.yiketianqi.com/...` | (none — HTTP to weather provider; leaks user's typed address) |

---

## 3 — Transport / checksum census across all UI actions

Every mouse-class UI action results in one of these transport calls:

| Wrapper                | Underlying gRPC | Checksum default | Used by |
|------------------------|-----------------|------------------|---------|
| `writeFeatureCmd(buf, BIT7)`   | `sendMsg` (interrupt-OUT)  | BIT7 by call site | every mouse setter + macro chunks + OTA chunks |
| `commomFeature(buf, BIT7)` (sic — "common" misspelled) | write then read | BIT7 by call site | every mouse getter (`getMouseOption0`, `getMouseOption1`, `getReportRate`, `getMacro`, `getFirmwareVersion`, …) |
| `writeRawFeatureCmd(buf, BIT7)` | `sendRawFeature` (SET_REPORT) | BIT7 by call site | currently UNUSED on the AJ159 mouse path; reserved for raw feature reports |

> **`writeFeatureCmd` uses `sendMsg` (interrupt-OUT), NOT `sendRawFeature`
> (SET_REPORT)** on the mouse class — this is the opposite of what
> `aj_series_vendor.md` §gRPC catalogue suggested. The renderer at
> `js:726805` calls `no(devPath, t, a, i)` where `no` is the
> `sendMsg` wrapper (`js:56601`). For our backend the equivalent
> path is **HID interrupt-OUT (`hidapi_hid_write`)**, not
> `HidD_SetFeature` / feature SET_REPORT.

This is a **non-trivial change** vs our current backend:

- Our `ITransport::writeFeature()` uses `hid_send_feature_report` (SET_REPORT).
- Vendor uses `hid_write` (interrupt-OUT pipe).

**Decision needed**: switch to `hid_write` for AJ-series mouse, OR
verify with USBPcap that both paths reach the same firmware logic
(both deliver report-id-0x05 to the device — Windows/`hidapi_hidraw`
on Linux should normalise the difference).

The vendor `writeRawFeatureCmd` (currently unused on AJ159) actually
maps to `sendRawFeature` / SET_REPORT. So the protocol is:

- mouse: interrupt-OUT
- (some other classes): SET_REPORT

---

## 4 — QML UX surface we should mirror

Based on the vendor app's tabs (per the React component tree), our
QML pane should expose:

```
Mouse pane
 ├─ Profile selector (8 buttons; current highlighted)
 ├─ Tab: Buttons
 │   └─ Default-matrix visualisation + per-button picker
 │       (combo / mouse-button / system-function / macro / forbidden)
 ├─ Tab: DPI
 │   └─ 8-row table: [enable-checkbox] [DPI numeric] [is-active radio] [colour swatch]
 │   └─ "Save" button → one 0x54 packet (rather than per-row)
 ├─ Tab: Polling
 │   └─ Radio group: 125 / 250 / 500 / 1000 / [2000 / 4000 / 8000 if wired+supported]
 ├─ Tab: Sensor
 │   ├─ LOD: 3-step radio (1mm / 2mm / 3mm)
 │   ├─ X-Sensitivity slider (0..100)
 │   ├─ Y-Sensitivity slider (0..100)
 │   ├─ Motion-smoothing toggle
 │   ├─ Angle-snap toggle
 │   └─ Debounce slider (0..10 ms)
 ├─ Tab: Lighting (if isRgb || hasBatteryLED)
 │   ├─ Effect dropdown (LightOff/AlwaysOn/Breath/Wave/Neon/...)
 │   ├─ Colour picker (RGB)
 │   ├─ Brightness slider (0..6)
 │   ├─ Speed slider (0..4, animated effects only)
 │   ├─ Dazzle toggle
 │   ├─ Wheel-light off toggle
 │   └─ Battery LED: high-charge colour, low-charge colour, "on while charging" toggle
 ├─ Tab: Macros (20 slots)
 │   ├─ Slot list (numbered 0..19, with name)
 │   ├─ Event editor (per-row: keyboard/mouse-button/mouse-move/delay)
 │   ├─ Repeat-count stepper
 │   └─ Record button (live capture)
 ├─ Tab: Sleep / Power
 │   ├─ BT idle timeout (1..30 min, in 1-min steps)
 │   ├─ BT deep-sleep timeout (1..60 min)
 │   ├─ 2.4G idle timeout
 │   ├─ 2.4G deep-sleep timeout
 │   └─ Power-save toggle
 └─ Tab: About / Firmware
     ├─ Firmware version (from 0x80 query)
     ├─ "Update mouse firmware" button → mledUpgrade flow
     └─ "Update screen firmware" button → oledUpgrade flow (only if hasScreen)
```

---

## 5 — Save model: omnibus vs per-field

The vendor app has **no global "Save" button** — every UI input
change immediately fires the relevant `setMouseOption0` / `setMouseOption1`
call. This is wasteful on the wire (every slider drag emits a 64-byte
write at every step), and it causes visible RGB flicker on multi-LED
devices.

**Recommendation for our QML**: keep a `dirty` flag per pane and
expose a single `[Save]` button per tab. Only on save, build the full
omnibus packet and emit one 0x53 write. This:

- reduces wire traffic by 10–100×;
- avoids RGB flicker on the device;
- maps cleanly to our `AjSeriesOptionPacket` builder (see
  `aj_series_opcode_table.md` §6.2);
- matches what most non-AJAZZ vendors do.

---

## 6 — Code corrections required

This document is informational for UI design. No source file changes
are direct consequences of this doc. However, **the QML pane structure
above should be reflected in**:

| File | Change |
|------|--------|
| `src/ui/qml/MousePage.qml` (or equivalent) | Add the tab-by-tab pane structure listed in §4. |
| `src/devices/mouse/include/ajazz/mouse/mouse.hpp` (`IMouseCapable`) | Add an `setMouseOption0(AjSeriesOptionPacket)` "save everything" method to enable the omnibus-write UX (§5). |
| `src/profiles/profile.hpp` | Add a `bool isDirty()` member used by the UI to enable/disable the Save button per pane. |
| `docs/research/feature-matrix.md` | Cross-reference this UI map under "mouse" → "UI features". |

### Catch2 tests

Out of scope for this UI doc — relevant tests are in the per-opcode doc
(`aj_series_opcode_table.md` §6.3).

A QML / UI test would live under `tests/ui/` using `QtQuickTest` —
that's an integration concern not covered here.

---

## 7 — References

| Subject | File:Line |
|---------|-----------|
| Renderer device store (`deviceStore.currentDev` model) | `dist/static/js/main_beautified.js:979100–982000` |
| `setDeBounce` (mouse path → `setMouseOption0`) | `…:979124` |
| `setSleepTime` (mouse path → `setMouseOption0`) | `…:979266` |
| `setReportRate` (mouse path → `setMouseOption0`) | `…:979362` |
| `setOpenBtn` (master light-off toggle) | `…:979420` |
| `setLightSetting` (RGB editor → 0x07) | `…:980049, 980282` |
| `setCurrentProfile` (profile dropdown) | `…:980818` |
| `setMacro` (macro save) | `…:980894, 981054, 981317` |
| `setKeyConfigSimple` (button rebind) | `…:981295, 1050722, 1060837` |
| `setFnKeyConfigSimple` (Fn-layer rebind) | `…:1050792, 1063198` |
| LOD radios | `…:1035792, 1035804, 1035816` |
| Angle-snap toggle | `…:1035834` |
| Motion-smoothing toggle | `…:1035846` |
| X/Y sensitivity sliders | `…:1035710, 1035721` |
| Polling rate radio buttons (mouse pane) | `…:1035881, 1046043, 1046057` |
| Button-change (wheel) | `…:1035741, 1035752, 1035763` |
| Battery-LED colour edit | `…:1036124, 1036142` |
| DPI table edit | `…:1037531, 1037555` |
| `getFirmwareVersion` UI call | `…:970959` |
| `setReportRate` (called from "8K" macro button) | `…:971588` |
