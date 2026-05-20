# Feature Research — v1.2 Connected-Device Capability Parity

**Domain:** Per-device capability parity for the 4 physically-connected AJAZZ devices (AKP05E Stream Dock Plus — 10 LCD keys / 4 endless encoders / touch strip, AK980 PRO keyboard, 2.4G 8K mouse, unknown Microdia PID).
**Researched:** 2026-05-15
**Confidence:** MEDIUM-HIGH for the three catalogued devices (OSS corpora — `mirajazz`, `opendeck-akp03`, `TaxMachine/ajazz-keyboard-software-linux` — provide concrete wire-format references for ≥80 % of the advertised capability surface). LOW-MEDIUM for the unknown PID 0c45:7016 (identification methodology is concrete, but the exact SKU is still pending a paired-input capture).

> **Scope discipline.** This file describes what the **native AJAZZ control applications** do for each device, normalised against the project's clean-room reverse-engineering rule (CLAUDE.md: no `wine`, no `innoextract`, no Delphi installer execution). Every protocol assertion below cites either (a) a free-software corpus that has captured the protocol independently, or (b) a USB descriptor read live from the user's machine. Speculative items are tagged `[SPECULATIVE — capture needed]`.

______________________________________________________________________

## Confirmed connected devices (live `lsusb`, 2026-05-15)

| Codename                                  | VID:PID     | USB topology                          | iManufacturer / iProduct                                         | Interfaces                                                                                                                                                                           | Current maturity | Advertised caps                    |
| ----------------------------------------- | ----------- | ------------------------------------- | ---------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ---------------- | ---------------------------------- |
| `akp05e`                      | `0300:3004` | `usb1/1-13/1-13.3/1-13.3.4`           | `HOTSPOTEKUSB` / `HOTSPOTEKUSB HID DEMO`, iSerial `0300D078140B` | If 0 = vendor HID (EP 0x82 IN 512-B, **EP 0x03 OUT 1024-B**); If 1 = boot keyboard 8-B EP                                                                                            | scaffolded       | `display`, `encoder`, `clock`      |
| `ak980pro`                                | `0c45:8009` | `usb1/1-10` (direct)                  | `SONiX` / `AK980 PRO` (bcdDevice 1.06)                           | If 0 = boot keyboard 64-B EP 0x81 IN; If 1 = mouse passthrough 64-B EP 0x82 IN; If 2 = vendor HID **EP 0x84 IN + EP 0x03 OUT** (control); If 3 = vendor HID 64-B EP 0x86 IN (status) | scaffolded       | `rgb`, `macros`, `layers`, `clock` |
| `ajazz_24g_8k`                            | `3151:5007` | `usb1/1-13/1-13.2`                    | (empty manuf) / `AJAZZ 2.4G 8K` (bcdDevice 0.02)                 | If 0 = boot mouse 64-B; If 1 = boot keyboard 64-B (consumer/media passthrough); If 2 = vendor HID (config channel)                                                                   | scaffolded       | `dpi`, `rgb`                       |
| **NEW** `microdia_dongle_7016` (proposed) | `0c45:7016` | `usb1/1-13/1-13.1/1-13.1.2` (via hub) | `SONiX` / `USB DEVICE` (bcdDevice 1.03)                          | If 0 = boot keyboard 8-B EP; If 1 = boot keyboard 8-B EP — **TWO boot-keyboard interfaces, no vendor channel**                                                                       | (not catalogued) | (unknown — pending identification) |

**Critical topology finding (refutes one ARCH hypothesis up front):** `0c45:7016` is on USB hub `1-13/1-13.1` and `0c45:8009` (AK980 PRO) is direct on `1-10`. They are **NOT** two interfaces of the same physical device. The mystery PID is **a separate wireless receiver dongle**, almost certainly bundled with a wireless keyboard/keypad paired into the dongle (the signature of "two HID boot-keyboard interfaces with 8-byte EPs" is the canonical Microdia/SONiX 2.4 GHz receiver pattern; see Sagacious 2013 post documenting `0c45:7000` as exactly this for iPazzPort KP-810-18BR). The AK980 PRO's tri-mode (BT/2.4G/USB-C) is wholly self-contained — when running in 2.4 GHz it presents its own dongle as `0c45:8009`, not `7016`. So `0c45:7016` is a **separate, third-party / older AJAZZ wireless input** sitting on the user's bus. See § 4.4 below for full investigation methodology.

______________________________________________________________________

## Feature Landscape (per-device)

For each device, the **Table Stakes** column is "what the native AJAZZ app does that users will notice missing"; **Differentiators** is "what we can do better"; **Anti-Features** is "what the native app does that we explicitly reject" (cross-checked against PROJECT.md Out of Scope — items already documented there are NOT re-listed here).

### 1. AKP05E (0x3004) — Stream Dock Plus (10 LCD keys / 4 endless encoders / LCD touch strip), scaffolded → functional

**Behavioral model (from `4ndv/opendeck-akp03` + `mirajazz` v0.12.1, HIGH confidence):**

The 0x3004 SKU's USB descriptor (`bNumInterfaces=2`, IF 0 vendor HID with **OUT EP wMaxPacketSize=1024 bytes**, IF 1 boot-keyboard passthrough) matches the AKP03-family `is_v2_api()` shape exactly per `[ajazz-sdk]` and `mirajazz::Device::connect(protocol_version=2 or 3, …)`. The 1024-byte OUT EP is the load-bearing v2/v3 signature; protocol version 3 adds full press+release pairs.

The vendor app (StreamDock by Mirabox / "AJAZZ App"):

- Treats the device as **3 rows × 3 cols = 9 logical buttons** (6 with LCD + 3 non-LCD side buttons), 3 rotary encoders, all pressable.
- Sends per-key JPEGs (60×60 `Rot0` for AKP03 family, 64×64 `Rot90` for AKP03R rev. 2 — protocol version 3 devices).
- Sends a global brightness 0..100 via the `LIG` opcode.
- Sends per-knob RGB color via the `SETLB` opcode (knob-LED color is a real per-encoder feature — **not** in our current `devices.yaml` advertised capability list).
- Shows a boot logo (320×240 JPEG `Rot90`) on a small front strip via the `LOG` opcode.
- Has **no host-settable RTC**. The vendor-app "clock widget" is a rendered-on-keyface image, identical to the AKP03 family pattern (see Out of Scope discussion in § 6 below).

**Wire-protocol opcodes** (from `mirajazz` v0.12.1, `src/device.rs`, MIT-licensed):

| Opcode    | Bytes (offset 6..8)                  | Purpose                                              | Direction               |
| --------- | ------------------------------------ | ---------------------------------------------------- | ----------------------- |
| `DIS`     | `0x44 0x49 0x53`                     | Initialize / enable display                          | Host → Device           |
| `LIG`     | `0x4c 0x49 0x47`                     | Set global brightness (byte 11 = 0..100)             | Host → Device           |
| `LBLIG`   | `0x4c 0x42 0x4c 0x49 0x47`           | Set per-knob LED brightness                          | Host → Device           |
| `SETLB`   | `0x53 0x45 0x54 0x4c 0x42`           | Set per-knob LED RGB color (3 bytes per knob × N)    | Host → Device           |
| `BAT`     | `0x42 0x41 0x54`                     | Image upload (JPEG payload follows, big-endian size) | Host → Device, chunked  |
| `CLE`     | `0x43 0x4c 0x45`                     | Clear key (byte 13: key+1 or 0xFF=all)               | Host → Device           |
| `STP`     | `0x53 0x54 0x50`                     | Commit / page-flip (v2/v3 only)                      | Host → Device           |
| `HAN`     | `0x48 0x41 0x4e`                     | Sleep                                                | Host → Device           |
| `CONNECT` | `0x43 0x4F 0x4E 0x4E 0x45 0x43 0x54` | Keep-alive ping                                      | Host → Device, periodic |
| (none)    | —                                    | **No `setTime` / RTC / clock opcode exists.**        | —                       |

#### Table Stakes (Users Expect These)

| Feature                                                        | Why Expected                                                                                                                                                                                                                                                                                            | Complexity | Notes / dependencies on existing code                                                                                                                                                                                                                                                                                                                                                    |
| -------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Per-key JPEG upload (60×60 `Rot0`, chunked over 1024-B OUT EP) | Functional sibling AKP03 / AKP03E / AKP03R already does this; the 0x3004 device's descriptor proves the wire is identical (`mirajazz::Device::connect(2, KEY_COUNT, ENCODER_COUNT)` produced by `4ndv/opendeck-akp03` would work as-is).                                                                | LOW        | `akp03_protocol.hpp` already defines `CmdImage = "BAT"` and `PacketSize=512` — needs **upgrade to 1024-byte packets** per `[ajazz-sdk]/is_v2_api` (TODO `AKP03 v2 framing migration`). REQ-IDs already exist in the v1.1 audit, just need an execute on `akp03.cpp`. Depends on Phase 4 hot-plug + Phase 8 maturity tier promotion.                                                      |
| Global brightness (0..100 via `LIG`)                           | Every other Stream-Dock-family device exposes this; the vendor app ships a brightness slider.                                                                                                                                                                                                           | LOW        | `buildSetBrightness` already in `akp03_protocol.hpp` — just needs the 1024-B framing upgrade.                                                                                                                                                                                                                                                                                            |
| Read button + encoder events with action codes from § above    | Without these the device looks dead in our app even though it's connected. The 0x3004 input report semantics follow the Stream Dock family wire (per `opendeck-akp03/src/inputs.rs`): LCD keys (10 on AKP05E, key codes ascending from 0x01), side buttons 0x25/0x30/0x31, encoders (4 endless encoders on AKP05E) press + 0x90/0x91/0x50/0x51/0x60/0x61 rotation. | LOW        | `parseInputReport` already in `akp03_protocol.hpp` — the action-code table is correct. Existing AKP03 `partial→functional` plan covers exactly this with the same wire format. Builds on Phase 4 `shared_ptr<IDevice>` lifecycle.                                                                                                                                                        |
| Synthesize missing press/release edges                         | AKP03 v2 protocol emits side-button events on release only; rotation events have no release; LCD keys may emit one frame per transition. Companion synthesises the missing edge to keep input semantics uniform.                                                                                        | LOW        | Already worked around in `akp03.cpp:289-293` with `EncoderReleased → EncoderPressed value=0` half-step. Vendor-protocol-notes Finding 16.E. Plan migrates this to a proper `core::DeviceEvent::Kind::EncoderReleased` variant — tracking entry `EncoderReleased proper event kind` in `TODO.md`.                                                                                         |
| Boot-logo upload on first connect                              | Vendor app sets a branded boot logo (320×240 JPEG `Rot90`) when the device pairs with the desktop software.                                                                                                                                                                                             | LOW        | Same `BAT` opcode path with `LOG` (`0x4C 0x4F 0x47`) instead. Already in `akp03.md` "Required for `stable`" column.                                                                                                                                                                                                                                                                      |
| Honest `clock` capability behavior                             | The 0x3004 device advertises `clock` in our `devices.yaml`, but `mirajazz` v0.12.1 has **no** `set_time` / `RTC` opcode. Native AJAZZ app shows "Clock" only as a rendered widget on key faces (image upload). Continuing to advertise `clock` would lie.                                               | LOW        | **REQ-EX (proposed for v1.2)**: demote `clock` from `akp05e` `capabilities` list in `devices.yaml`, OR keep `hasClock=true` but reroute the "Sync" button to a render-clock-on-key-face widget (different feature surface entirely). PROJECT.md Out of Scope already says "Render-time-on-keyface clock widget is a different feature" — so the honest path is **demotion**. |

#### Differentiators (Competitive Advantage)

| Feature                                                         | Value Proposition                                                                                                                                                                                                                                                                               | Complexity               | Notes                                                                                                                                                                                                                  |
| --------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Per-encoder RGB color via `SETLB` opcode                        | The native app changes the knob ring color when a knob is "active" (volume vs zoom). We can expose this as a configurable per-encoder color — none of the OSS Stream-Dock-family alternatives (streamdeck-linux-gui, OpenDeck, StreamController) expose per-knob RGB.                           | LOW (one-shot writes)    | New `EncoderRgbColor` field on `Profile::Encoder`. Builds on existing `IRgbCapable` mix-in. **Adds a `rgb` capability bit** to `akp05e` (currently advertised: `display, encoder, clock`).                 |
| Animated GIF on keys (multi-frame JPEG sequence)                | Vendor app supports animated GIF icons on AKP03 keys (`mirabox-n3` product page documents this; `mirajazz` v0.12.1 has the `flush()` + multi-frame `BAT` send already structurally ready). Differentiator vs Companion / OpenDeck which are still-image only.                                   | MEDIUM (frame scheduler) | Builds on `display` capability. Tracking entry: `TODO.md → "AKP03 animated GIF support"` (not present today, propose adding). Per `akp03.md` "Features that must work" table this is `nice-to-have` for `stable` tier. |
| 0x3004 SKU probe → known-family fallback if descriptor mismatch | The 0x3004 PID is "Ajazz HOTSPOTEKUSB HID DEMO" — bcdDevice = 0.02, iSerial = `0300D078140B`. This is likely a pre-production / engineering-sample unit. Probe firmware version (Feature Report `0x01`) and surface "engineering sample" badge if firmware string doesn't match any retail SKU. | MEDIUM                   | New `IDevice::probe()` hook surfaces firmware fingerprint as a non-blocking signal. Listed in v1.1 research as Tier-3 differentiator; now becomes relevant for the 0x3004 unit.                                        |

#### Anti-Features (Commonly Requested, Often Problematic) — incremental to PROJECT.md Out of Scope

| Feature                                                          | Why Requested                      | Why Problematic                                                                                                                                                                                                                                                                                 | Alternative                                                                                                                                                                                                         |
| ---------------------------------------------------------------- | ---------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Synthesizing a `setSystemTimeOn` wire format for AKP03 family    | "Honor the `clock` capability bit" | The AKP03 protocol DEMONSTRABLY has no RTC opcode (`mirajazz` codepath, opendeck-akp03 codepath, our own `akp03.md` opcode table all confirm). Inventing a packet would be fiction, not clean-room — and there's no host-RTC to set on the device firmware (its time display is renderer-side). | **Demote `clock` capability** from `akp05e` in `devices.yaml`. Update `register.cpp` `hasClock=false`. UI Sync button hides cleanly via the existing capability-gating role from Phase 5 (TIMESYNC-04). |
| Adding a separate parallel backend for the 0x3004 device         | "The serial says `HID DEMO`!"      | The 0x3004 device shares the Stream Dock family wire (1024-byte OUT EP, same iManufacturer prefix, same `bNumInterfaces=2`); it does NOT need a bespoke transport stack. It is a distinct geometry (AKP05E Stream Dock Plus: 10 keys / 4 encoders / touch strip) routed via the family's `makeAkp05` factory.                      | Route through the shared Stream Dock backend via the `makeAkp05` factory in `register.cpp` (firmware-confirmed AKP05E, codename `akp05e`), promote tier alongside the rest of the family.                                                                          |

______________________________________________________________________

### 2. AJAZZ AK980 PRO (`0c45:8009`) — TFT-display keyboard, scaffolded → partial/functional

**Behavioral model (from `TaxMachine/ajazz-keyboard-software-linux` AK820 Pro implementation + AJAZZ product pages, MEDIUM-HIGH confidence):**

The AK980 PRO is a 97/98-key tri-mode (BT 5.0 / 2.4 GHz / USB-C wired) hot-swappable mechanical keyboard with an **8000 mAh battery** and an integrated **1.14" TFT color screen** that shows time, connection mode, battery, RGB mode, AND can render custom GIF animations ([MechLands product page](https://www.mechlands.com/products/ajazz-ak980)). It uses the SONiX SN32-family controller chip (Microdia VID `0c45`).

**Critical TaxMachine finding:** The AJAZZ AK820 Pro shares VID `0c45` AND PID `0x8009` (same as our AK980 PRO). Either (a) AJAZZ reuses the PID across the AK820/AK980 PRO line, distinguishing by `iProduct` string only, or (b) the AK820 Pro Linux project mislabels the SKU. **Either way, the protocol captured against AK820 Pro is directly applicable to AK980 PRO** — same chip family, same VID:PID, same descriptor shape (4 interfaces, with IF 2 being the vendor channel having paired IN+OUT 64-byte EPs).

**Wire protocol (TaxMachine clean-room capture):**

- Transport: HID **Feature Reports** (`hid_send_feature_report` / `hid_get_feature_report`) on Interface 2 vendor HID channel. **Note:** This differs from the AKP03 family which uses interrupt OUT.
- Report ID byte 0: `0x04` (`COMMAND_PREFIX`) — **the same `0x04` already used by our existing `ProprietaryKeyboard` backend** (verified in `docs/protocols/keyboard/proprietary.md`).
- Packet size: 64 bytes.
- Three-stage command pattern: `START` (cmd 0x18) → command body → `FINISH` (cmd 0xf0). Each report is acknowledged with a `hid_get_feature_report`.

| Command         | Cmd byte | Purpose                                                                             |
| --------------- | -------- | ----------------------------------------------------------------------------------- |
| `START`         | `0x18`   | Begin command sequence (per-message, NOT session-level)                             |
| `FINISH`        | `0xf0`   | Commit command sequence                                                             |
| `MODE_COMMAND`  | `0x13`   | Set lighting effect (one of 20 modes — see § below)                                 |
| `IMAGE_COMMAND` | `0x72`   | Upload image/GIF to the 1.14" TFT (host-rendered clock/widget content)              |
| `SLEEP_TIME`    | `0x17`   | Set auto-sleep timeout (NONE / 1 min / 5 min / 30 min)                              |
| (none)          | —        | **No per-key RGB write captured** (TaxMachine has `setColor(r,g,b)` as a `// TODO`) |
| (none)          | —        | **No macro upload captured** (TaxMachine: `[ ] Macros` in TODO)                     |
| (none)          | —        | **No layer-switch command captured** (TaxMachine: not present)                      |
| (none)          | —        | **No clock / setTime / RTC command** (TaxMachine: not present in protocol)          |

**Lighting modes** (20 modes, TaxMachine `LightingMode` enum):
`LED_OFF`, `STATIC`, `SINGLE_ON`, `SINGLE_OFF`, `GLITTERING`, `FALLING`, `COLOURFUL`, `BREATH`, `SPECTRUM`, `OUTWARD`, `SCROLLING`, `ROLLING`, `ROTATING`, `EXPLODE`, `LAUNCH`, `RIPPLES`, `FLOWING`, `PULSATING`, `TILT`, `SHUTTLE`. Single global color (or `rainbow=true`), brightness `0..5` (NOT 0..100), speed `0..5`, direction (LEFT/UP/DOWN/RIGHT).

**ModePacket layout** (64 bytes):

```
byte 0       : 0x04 (COMMAND_PREFIX, report ID)
byte 1       : mode (0..19)
byte 2..4    : R, G, B
byte 5..8    : padding (0x00 × 4)
byte 9       : rainbow (bool)
byte 10      : brightness (0..5)
byte 11      : speed (0..5)
byte 12      : direction (0..3)
byte 13..14  : padding (0x00 × 2)
byte 15..16  : delimiter 0xaa55 (LE)
byte 17..63  : padding (zeros)
```

#### Table Stakes (Users Expect These)

| Feature                                                                           | Why Expected                                                                                                                                                                                                            | Complexity                                                                            | Notes / dependencies                                                                                                                                                                                                                                                                                                                                                |
| --------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| RGB **effect-mode** (one of 20 modes) + global color + brightness/speed/direction | This is the dominant feature on the vendor's product page (the "97-key RGB keyboard"). Every review of AK980 / AK980 PRO leads with the lighting modes. Native app exposes a 20-mode dropdown + color picker + sliders. | LOW (one Feature-Report write per mode change; protocol fully captured by TaxMachine) | New `IRgbCapable::setEffect(ModeId, r, g, b, brightness, speed, direction)` method shape. Migrates `proprietary_keyboard.cpp` from the generic VIA-superset wire format (currently models report 0x04 cmd 0x09 = `SET_RGB_EFFECT`) to the AK980-specific 0x04 cmd 0x13 family. Cleanly back-compatible because the 0x04 prefix is shared.                           |
| Sleep-timer configuration (NONE / 1 / 5 / 30 min)                                 | Battery life on this 8000 mAh keyboard is the #2 marketing claim; users WILL want to control sleep. Native app has a settings page for it.                                                                              | LOW                                                                                   | `setSleepTime(LightSleepTime)` already in TaxMachine reference. Trivial 64-B feature-report write.                                                                                                                                                                                                                                                                  |
| Upload custom image/GIF to the 1.14" TFT (used as host-rendered clock display)    | The TFT display is the AK980 PRO's headline differentiator vs. AK980. Vendor app uploads animated content. Without this we can't render anything on the screen.                                                         | MEDIUM                                                                                | TaxMachine has the function signature and packet shape (`START_IMAGE = [0x04, 0x72, 0x02, ..., 0x09]`) but the **chunked send loop is `//TODO` commented-out**. Will need a paired-with-real-hardware capture to confirm chunk size + PNG-vs-raw-RGB encoding. Mark **`[SPECULATIVE — capture needed for chunk size and pixel format]`**.                           |
| Honest `clock` behavior — same architectural pattern as AKP03                     | The AK980 PRO **does** show a clock on the TFT, but as a host-pushed rendered image (vendor app sends "current time" → renders → uploads), not via a settable RTC opcode. The protocol has NO `set_time` command.       | LOW (consequence)                                                                     | Same Out-of-Scope rationale as AKP03 above: **demote `clock` capability** OR keep it advertised and re-route TimeSyncService's `setTime` callback to a "render time on TFT" pipeline (separate `IDisplayCapable` surface, NOT the `IClockCapable` surface). Decision needed in roadmapping. The honest path is demotion + a future `TftClockWidget` differentiator. |

#### Differentiators (Competitive Advantage)

| Feature                                                                 | Value Proposition                                                                                                                                                                                                            | Complexity  | Notes                                                                                                                                                                                                                                                                                           |
| ----------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Render-time-on-TFT clock widget                                         | Vendor app shows a fixed time digit format; we can offer multiple faces (analog, digital, custom). PROJECT.md Out of Scope already flags this as a "different feature" — but for the AK980 PRO it's exactly the right shape. | MEDIUM      | Builds on the captured `IMAGE_COMMAND = 0x72` upload path. Render time-as-image host-side, push periodically. **This is the natural home for the v1.1 `IClockCapable` scaffolding** if we re-route the auto-sync timer to drive image refresh instead of pure RTC writes. ARCH decision needed. |
| Per-key custom RGB (currently `[ ] Individual LEDs` in TaxMachine TODO) | Native app exposes per-key color via a paint-style UI. Vendor protocol-version unknown. **`[SPECULATIVE — capture needed]`**. If captured, ships as `partial` → `functional`.                                                | HIGH        | Requires a host-side per-key buffer + a custom-RGB upload opcode (NOT 0x13 = effect-mode). Likely a chunked write similar to `IMAGE_COMMAND`, but byte layout unknown. Best done after v1.2 functional baseline lands, NOT in the initial promotion phase.                                      |
| Macros (record + playback on-device)                                    | Native app advertises "macros" prominently. Stored per-key on the keyboard EEPROM. Protocol unknown. **`[SPECULATIVE — capture needed]`**.                                                                                   | HIGH        | Existing `proprietary_keyboard.cpp` has a SET_KEYCODE(layer,row,col) + UPLOAD_MACRO(slot,off,len) command shape for AK820 (`0x05` / `0x0D`) — likely a different protocol_version than what AK980 PRO uses, since AK820 protocol per TaxMachine uses 0x04 prefix not 0x05. Need direct capture. |
| Layer-switching (4 layers per `proprietary.md`)                         | Vendor app exposes layer switching. Already in our `devices.yaml` advertised capability list. Protocol unknown for AK980 PRO specifically.                                                                                   | MEDIUM-HIGH | `proprietary.md` documents `SET_LAYER(layer) = 0x0C`. **`[SPECULATIVE — need to verify the AK980 PRO obeys the same layer opcode as AK820 Pro]`**.                                                                                                                                              |
| Wireless battery readback                                               | Standard expectation for a wireless keyboard. None of the OSS competitors expose this for AJAZZ; native app does. Protocol unknown.                                                                                          | MEDIUM      | Likely a Feature Report read (analogous to AJ-series mouse `kCmdBattery = 0x40`). **`[SPECULATIVE — capture needed]`**.                                                                                                                                                                         |

#### Anti-Features (Commonly Requested, Often Problematic) — incremental to PROJECT.md Out of Scope

| Feature                                                | Why Requested                                                     | Why Problematic                                                                                                                                                                                                                              | Alternative                                                                                                                                                                                                                                   |
| ------------------------------------------------------ | ----------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Brightness 0..100 normalization                        | "Match the slider granularity of the AKP03 family for code reuse" | AK980 PRO firmware accepts only `0..5` (5 levels). Exposing a 0..100 slider that internally rounds is dishonest at the UX seam. Vendor app uses a 5-step slider.                                                                             | Keep the keyboard's native granularity; provide a separate `KeyboardBrightness` model role (0..5) distinct from `DisplayBrightness` (0..100 for stream decks).                                                                                |
| Force-pair flow via the dongle                         | "Replicate native pairing UX"                                     | The AK980 PRO's tri-mode is fully driven by an on-device button combo (Fn+1..3 typically). Reimplementing pairing in software would require a captured pair-mode opcode we don't have, and the user's keyboard's button combo already works. | Document the on-device button combo in the in-app device tooltip; do not attempt to drive pairing from software.                                                                                                                              |
| Trying to capture protocol from vendor `.exe` via wine | "Faster than capturing live USB traffic"                          | CLAUDE.md hard rule: no `wine`, no Delphi installers. PROJECT.md Out of Scope: vendor-driver bundling.                                                                                                                                       | The TaxMachine project documents Wireshark + USBPcap capture against the **Windows binary running on a Windows host**, then the OSS Linux side re-implements from observed packets. Repeat the discipline on a separate Windows VM if needed. |

______________________________________________________________________

### 3. AJAZZ 2.4G 8K mouse (`3151:5007`) — wireless 8000 Hz polling, scaffolded → functional

**Behavioral model (from `aj_series.cpp` existing backend + AJAZZ product pages for AJ159 APEX, MEDIUM confidence):**

The mouse advertises an 8000 Hz polling rate in 2.4 GHz wireless mode (1000 Hz wired), PAW3950 sensor (or PAW3395 depending on SKU variant), 6 DPI stages with per-stage color, and per-zone RGB (scroll wheel + logo). It's on SONiX VID `3151` (which the `proprietary.cpp` keyboard backend also uses for VIA-style boards — same chip family). The Linux kernel HID stack is `hidraw`-capable at any polling rate the device negotiates; **8 kHz polling does NOT require a special host-side negotiation** — the device's own descriptor + EP `bInterval=1` does the work.

**Wire protocol (existing `aj_series.cpp`, 2026-04-26 capture, HIGH confidence):**

- Transport: **Feature Reports** on the vendor-class HID interface (IF 2 per descriptor inspection).
- Report ID byte 0: `0x05`.
- Packet size: 64 bytes.
- Layout: `[0x05][cmd][sub][len][payload…59 bytes…][checksum]`. Checksum = 8-bit sum of bytes 1..62.

| Command (byte 1)    | Purpose                                             |
| ------------------- | --------------------------------------------------- |
| `0x21` kCmdDpi      | Configure DPI stages + per-stage indicator color    |
| `0x22` kCmdPollRate | Set USB polling rate (Hz)                           |
| `0x23` kCmdLod      | Set lift-off distance (0.1 mm units)                |
| `0x24` kCmdButton   | Bind a mouse button to a HID action                 |
| `0x30` kCmdRgb      | Control RGB lighting (static / effect / brightness) |
| `0x40` kCmdBattery  | Query battery level (wireless models)               |
| `0x50` kCmdCommit   | Persist to EEPROM                                   |

**Critical reconciliation flag (from `vendor-protocol-notes.md` Finding 11):** AJ199 V1.0 and AJ199 Max have structurally different wire formats (offset-based struct vs flat report). The 8K mouse may be in either camp until a direct capture confirms. The existing backend is wired to the AJ199 V1.0 shape; the 8K mouse currently uses this same backend (`devices.yaml:protocol_doc: docs/protocols/mouse/aj_series.md`). **A probe-and-validate step is required before promoting to `functional`.**

#### Table Stakes (Users Expect These)

| Feature                                                         | Why Expected                                                                                                                      | Complexity | Notes / dependencies                                                                                                                                                                                                                                               |
| --------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------- | ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 6 DPI stages with per-stage configurable values + colors        | Headline feature of the SKU. Vendor app shows a stage-table editor. Without it the device feels broken.                           | LOW        | `aj_series.cpp::stageDpiTable` already implements this; just needs probe-and-confirm against real device. Builds on existing `IMouseCapable`.                                                                                                                      |
| RGB on visible zones (scroll wheel + logo, possibly side strip) | Aesthetic baseline for the gaming-mouse segment. Vendor app exposes color picker + breathe / cycle / static modes.                | LOW        | `aj_series.cpp::kCmdRgb (0x30)` already implements this. Needs verification that the zone IDs the AJ-series backend uses are correct for the 8K mouse.                                                                                                             |
| Polling-rate selection (1k / 2k / 4k / 8k for wireless)         | Headline differentiator from the 1k-only competition. Vendor app exposes a dropdown. Mandatory for "8K mouse" branding.           | LOW        | `aj_series.cpp::kCmdPollRate (0x22)` already implements this. **Linux note: hidraw read loop must keep up at 8 kHz** — the project's `hidapi_hidraw` backend has been benchmarked at >10 kHz read rates on modern kernels (>=5.10), so no kernel-level rate limit. |
| Lift-off distance setting                                       | Standard feature on the gaming-mouse segment. Vendor app exposes a slider 1.0..2.0 mm.                                            | LOW        | `aj_series.cpp::kCmdLod (0x23)` already implements this.                                                                                                                                                                                                           |
| Button remap (especially sniper button if present)              | Standard feature. Vendor app exposes a per-button binding editor. The user can rebind side buttons / wheel-click / sniper button. | LOW-MEDIUM | `aj_series.cpp::kCmdButton (0x24)` already implements this. The 8K mouse's exact button count and roles need probe.                                                                                                                                                |
| Battery readback (wireless modes only)                          | The mouse is wireless; users need to know battery status. Vendor app shows a percentage indicator.                                | LOW        | `aj_series.cpp::kCmdBattery (0x40)` already implements this — but battery report-back may be cmd-dependent on whether the device is connected via dongle or direct. Probe-and-validate required.                                                                   |

#### Differentiators (Competitive Advantage)

| Feature                                         | Value Proposition                                                                                                                                     | Complexity | Notes                                                                                                                      |
| ----------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------- | ---------- | -------------------------------------------------------------------------------------------------------------------------- |
| OSD-style on-screen DPI / polling-rate feedback | When user presses a DPI cycle button, host-side OSD shows current stage. None of the OSS mouse-config tools (Piper, ratbagd) do this for AJAZZ.       | MEDIUM     | Builds on existing event-routing pipeline; needs a new Qt overlay window.                                                  |
| Profile auto-switch by foreground window class  | Vendor app may or may not do this; we can. "Use profile A when game X is focused."                                                                    | MEDIUM     | Requires Wayland/X11 window-class polling (Wayland: AT-SPI, X11: `_NET_ACTIVE_WINDOW`). Cross-platform abstraction needed. |
| Honest "polling rate cap detected" warning      | If user plugs into a USB 2.0 port that can't sustain 8 kHz, native app silently degrades. We surface a tooltip "USB 2.0 detected, max 4 kHz polling". | LOW-MEDIUM | Reads `bcdUSB` from the device descriptor; surfaces in sidebar. Reuses Phase 8 maturity tooltip pattern.                   |

#### Anti-Features (Commonly Requested, Often Problematic)

| Feature                                                   | Why Requested        | Why Problematic                                                                                                                                                                                                                             | Alternative                                                                                                                                                            |
| --------------------------------------------------------- | -------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| In-app DPI-change toast on every press                    | "Feedback"           | Spam at 8 kHz polling if the firmware emits repeats. Also fatigue-inducing.                                                                                                                                                                 | OSD overlay that auto-dismisses (differentiator above) — only shows on actual stage transitions, debounced.                                                            |
| Polling-rate dynamic-throttling-when-idle                 | "Save battery"       | The mouse already has its own sleep mode; layering host-side throttle creates double-debounce headaches and breaks low-latency claims.                                                                                                      | Leave it to the firmware; expose the on-device sleep-time only.                                                                                                        |
| Pretending the wired and dongle modes are the same device | "Single sidebar row" | They have different VID:PID (`3151:5007` for the live device per `lsusb`; the AJ-series catalogue includes `248A:5C2F` / `249A:5C2F` for dongles). Conflating means battery readback would fire on a wired connection and confuse the user. | Two separate `devices.yaml` rows (already the convention for AJ-series); use codename suffix `_wired` / `_dongle`. The 8K mouse's wired-mode VID:PID is not yet known. |

______________________________________________________________________

### 4. Unknown PID `0c45:7016` — investigation methodology, scaffolded entry

**The deliverable here is a concrete investigation methodology, not a behavioral spec — because the PID is not in our catalogue, not in any public USB-ID database I could find (`usb-ids.gowdy.us`, `devicekb.com`, `devicehunt.com` all return either no match or only nearby PIDs in the same `7xxx` Microdia range), and not in any of the OSS reverse-engineering corpora.**

**What we know from live descriptor inspection (`lsusb -d 0c45:7016 -v`):**

| Field                  | Value                                                                                         |
| ---------------------- | --------------------------------------------------------------------------------------------- |
| VID:PID                | `0c45:7016` (Microdia vendor, unregistered PID)                                               |
| iManufacturer          | `SONiX`                                                                                       |
| iProduct               | `USB DEVICE` (the generic SONiX placeholder string)                                           |
| bcdDevice              | `1.03`                                                                                        |
| Negotiated speed       | **Full Speed (12 Mbps)** — NOT High Speed                                                     |
| bNumInterfaces         | 2                                                                                             |
| Interface 0            | HID, BootInterface=1, Protocol=1 (**Keyboard**), 1 endpoint, EP 0x81 IN 8 bytes, bInterval=1  |
| Interface 1            | HID, BootInterface=1, Protocol=1 (**Keyboard**), 1 endpoint, EP 0x83 IN 8 bytes, bInterval=10 |
| USB topology           | On hub `usb1/1-13/1-13.1`, **not** on the AK980 PRO's `usb1/1-10` branch                      |
| Power                  | Bus-powered, Remote Wakeup capable, 500 mA                                                    |
| Report descriptor size | IF 0 = 79 bytes, IF 1 = 144 bytes                                                             |

**Pattern signature: this is a wireless 2.4 GHz USB receiver dongle.** Three converging pieces of evidence:

1. **Two boot-keyboard interfaces with low-bandwidth 8-byte EPs** is the canonical Microdia/SONiX wireless receiver topology — confirmed by Sagacious's 2013 documentation of `0c45:7000` ("receiver for iPazzPort Commander Bluetooth KP-810-18BR"). The Microdia `0c45:70xx` range is heavily populated by SONiX OEM dongles.
1. **Full-Speed (12 Mbps) not High-Speed**. Real keyboards/mice run High-Speed if the chip supports it; receivers run at the lower negotiation speed.
1. **iProduct = "USB DEVICE"** with no SKU-distinguishing string is the OEM placeholder — the same SONiX/Microdia chip family is used in dozens of generic Chinese wireless keyboards/keypads/numpads. The downstream wireless device's identifier is not exposed at the USB layer; it would only appear in the HID report descriptor or in the report payload after a key event.

**It is NOT a secondary interface of the AK980 PRO** because:

- AK980 PRO is on `usb1/1-10` (no hub between it and root); 7016 is on `usb1/1-13/1-13.1/1-13.1.2` (hub-mediated, different topology branch entirely).
- AK980 PRO's tri-mode (BT/2.4G/wired) presents itself as `0c45:8009` in any of the three modes — switching modes doesn't change VID:PID per the AJAZZ product page.

#### Concrete identification methodology

| Step | Action                                                                                                                                                                                                                                                                      | Output                                                   |
| ---- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------- |
| 1    | `cat /sys/bus/usb/devices/1-13.1.2/{idVendor,idProduct,product,manufacturer,bcdDevice,bNumInterfaces}` plus `udevadm info -a -n /dev/hidraw5` and `/dev/hidraw6`. **DONE — output above.**                                                                                  | Descriptor metadata.                                     |
| 2    | Capture HID report descriptors with root-equivalent `sudo cat /sys/class/hidraw/hidraw5/device/report_descriptor` (or `hidrd-convert` for human-readable form). Compare to known SONiX wireless-receiver descriptors in the `SonixQMK/Mechanical-Keyboard-Database` corpus. | Concrete RD bytes; possibly a chip-family fingerprint.   |
| 3    | Open hidraw, run `evtest` or `hexdump < /dev/hidraw5`, type on any paired wireless keyboard you might have. If a keypress fires, the dongle has a paired downstream device. Note: this must be done by the user since the `0c45:7016` device has `uaccess` udev tag.        | Identification of the paired downstream device (if any). |
| 4    | If no input ever arrives, the dongle is unpaired or its downstream pair has been long-removed. Add to `devices.yaml` as **`probed` not `scaffolded`** since we have the descriptor but no protocol behavior.                                                                | Catalogue entry decision.                                |
| 5    | Cross-reference the chip family with `SonixQMK/sonix_dumper` if a deeper protocol probe is warranted. `sn32f248/sn32f268/SN8P` are the candidate chip families that ship with this kind of descriptor.                                                                      | Chip family identification (informational only).         |

#### Proposed `devices.yaml` entry (after step 1 above)

```yaml
- codename: microdia_dongle_7016
  family: keyboard           # canonical for HID boot-keyboard descriptor; revisit if dongle proves to multiplex
  name: "Microdia/SONiX wireless receiver (PID 0x7016)"
  vid: "0x0c45"
  pid: "0x7016"
  maturity: probed           # descriptor confirmed; protocol unknown until paired-device capture
  capabilities: []           # no advertised capability until protocol mapping arrives
  protocol_doc: docs/protocols/keyboard/microdia_dongle.md  # new doc, stub-only
  notes: |
    Wireless 2.4 GHz USB receiver, two boot-keyboard HID interfaces (8-byte EPs, Full-Speed),
    iProduct="USB DEVICE", iManufacturer="SONiX". Likely OEM dongle for a separate AJAZZ
    (or compatible Chinese-OEM) wireless keyboard/keypad. NOT a secondary interface of the
    AK980 PRO (different USB topology, separate enumeration). Surfaced via live hot-plug
    capture 2026-05-15 alongside AK980 PRO + AKP03 + 8K mouse.
    Tracking: TODO.md → "Microdia 0c45:7016 paired-device identification".
```

**Outcome category: `probed` tier, no capability commitment, no protocol implementation in this milestone.** Promotion to `partial` waits for either (a) a paired-downstream-device identification + capture, or (b) a vendor manifest hit when AJAZZ's online catalogue is re-enumerated.

#### Anti-features for 0c45:7016 specifically

| Feature                                                                | Why Requested          | Why Problematic                                                                                                       | Alternative                                                                                            |
| ---------------------------------------------------------------------- | ---------------------- | --------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------ |
| Speculative protocol implementation based on "looks like a SONiX chip" | "Get it working fast"  | Without the report descriptor and a paired device, any wire format would be invented. Violates clean-room discipline. | Stay at `probed` tier; document the descriptor; await capture.                                         |
| Auto-marrying it to the AK980 PRO sidebar row                          | "Looks tidy in the UI" | Wrong — they're separate USB devices with separate hot-plug lifecycles. The user could unplug one and not the other.  | Keep them as two distinct sidebar rows. The hot-plug + offline-badge UX already supports this cleanly. |

______________________________________________________________________

## Feature Dependencies

```
AKP03 v2 framing (1024-B packets)               AK820/AK980 vendor channel (IF 2)
        │                                              │
        │ enables                                      │ enables
        ▼                                              ▼
AKP05E 0x3004 image upload                      AK980 PRO RGB modes (0x13)
        │                                              │
        │ enables                                      │ enables
        ▼                                              ▼
AKP05E 0x3004 boot-logo + key images           AK980 PRO TFT image upload (0x72)
        │                                              │
        │ co-requires                                  │ co-requires
        ▼                                              ▼
Phase 4 shared_ptr<IDevice> lifecycle           Phase 5 TimeSyncService
        ▼                                              ▼
        │                                              │ which gates
        │                                              ▼
        │                                       Honest clock demotion OR TftClockWidget reroute (ARCH decision)

AKP05E 0x3004 input parsing
        │
        │ requires
        ▼
EncoderReleased proper event kind in core::DeviceEvent::Kind
        │
        │ unblocks
        ▼
AKP03 partial → functional promotion

8K mouse probe-and-validate
        │
        │ requires
        ▼
AJ-series AJ199 V1.0 vs AJ199 Max protocol fork
        │
        │ informed by
        ▼
vendor-protocol-notes Finding 11.B

Microdia 0c45:7016 identification (descriptor + paired-device capture)
        │
        │ does NOT block
        ▼
Other three devices' promotion paths
```

### Dependency notes

- **AKP03 v2 framing upgrade is load-bearing** for promoting `akp05e` to `functional`. The existing `akp03.cpp` ships 512-byte packets; the device's actual OUT EP is `wMaxPacketSize=1024`. Confirmed by `[ajazz-sdk]::is_v2_api()`, `mirajazz::Device::packet_size = 1024 if protocol_version >= 2`, and our own descriptor read. **This is the single change that unlocks the whole 0x3004 promotion.**
- **`EncoderReleased` core event** is shared infrastructure across AKP03 family — tracked in `TODO.md` already. Fix here naturally raises **all** AKP03 sibling devices' maturity, not just 0x3004.
- **AK980 PRO's `clock` capability demotion** is a load-bearing ARCH decision for v1.2. Either we demote `hasClock=true` from `register.cpp:60-63` (honest path, mirrors AKP03 demotion) OR we re-architect `IClockCapable::setTime` to call into `IDisplayCapable` for the TFT render path. The first is a 1-line code change + capability re-validation; the second is a bigger architectural slice. **Recommendation: demote in v1.2, leave TftClockWidget as a v1.3 differentiator.**
- **8K mouse's wire-format fork** (AJ199 V1.0 vs Max) is the lowest-confidence area of the three known devices. The mouse currently routes through the AJ-series backend with `protocol_doc: docs/protocols/mouse/aj_series.md`. A probe-and-confirm step is mandatory before promotion claims.
- **Microdia 0c45:7016 has no dependency on the other three.** It can be added to the catalogue as `probed` independently of any milestone scheduling.

______________________________________________________________________

## MVP Definition — for v1.2 connected-device parity

### Launch With (v1.2 must-have)

- [ ] **AKP03 v2 framing upgrade** (512-B → 1024-B packets) + framing-version detection by descriptor inspection. Unblocks 0x3004 promotion AND benefits AKP03E, AKP03R, AKP03R rev. 2, plus all 8 Mirabox / Soomfon / Mars Gaming / TreasLin / Redragon sibling SKUs.
- [ ] **`EncoderReleased` proper core event** (decoupled from the `EncoderPressed value=0` workaround in `akp03.cpp:289-293`).
- [ ] **AKP05E 0x3004 promotion to `functional`** — display + encoder both work end-to-end. Requires the v2 framing upgrade above.
- [ ] **AK980 PRO RGB mode + brightness + speed + direction** — 20-mode dropdown wired via the captured `0x04 / 0x13` command. Promotes `ak980pro` to `partial`.
- [ ] **AK980 PRO sleep-timer** — trivial Feature-Report write. Adds polish.
- [ ] **AJAZZ 2.4G 8K mouse probe-and-confirm** + DPI / poll-rate / LOD / button / RGB feature exercise against real hardware. Promotes `ajazz_24g_8k` to either `partial` (some features confirmed, others not) or `functional` (all confirmed).
- [ ] **AK980 PRO `clock` demotion** OR `TftClockWidget` arch decision — pick one in roadmapping; ship the chosen path.
- [ ] **AKP05E 0x3004 `clock` demotion** — `hasClock=false` in `register.cpp` for this variant (since the descriptor proves it's identical wire to AKP03 family which has no RTC opcode).
- [ ] **Microdia 0c45:7016 catalogue entry** at `probed` tier — descriptor-only commitment, no protocol claims.
- [ ] **Real-hardware visual UI verifications** carried over from v1.1: Phase 5 Sync button visibility, Settings auto-sync persistence across restart, auto-sync glyph-only-no-toast on arrival, Phase 8 MaturityRole tooltip — now unblocked by physical access to all 4 devices.

### Add After Validation (v1.2.x patch series)

- [ ] **AK980 PRO TFT image upload** — captured chunked-write loop fills in (`IMAGE_COMMAND = 0x72`). Required for any clock-widget or custom-content rendering.
- [ ] **AKP05E 0x3004 `SETLB` per-knob RGB** — extends advertised capability to include `rgb` for this device.
- [ ] **AKP03 animated-GIF on keys** — multi-frame `BAT` sends. Differentiator vs Companion / OpenDeck.
- [ ] **8K mouse OSD overlay** for DPI / polling rate.
- [ ] **AK980 PRO battery readback** — once the read-back wire format is captured.

### Future Consideration (v1.3+)

- [ ] **AK980 PRO per-key custom RGB** — high-effort capture; needs the per-key buffer opcode.
- [ ] **AK980 PRO macro upload** — high-effort capture; vendor protocol unknown.
- [ ] **AK980 PRO layer-switch verification** — confirm same opcode as `proprietary.md` AK820 family.
- [ ] **Microdia 0c45:7016 paired-device protocol mapping** — only after a paired wireless input is identified.
- [ ] **TftClockWidget for AK980 PRO** — render time-as-image on the 1.14" TFT. Requires v1.2.x TFT image upload to land first.

______________________________________________________________________

## Feature Prioritization Matrix

| Feature                                                              | User Value                      | Implementation Cost       | Priority                                    |
| -------------------------------------------------------------------- | ------------------------------- | ------------------------- | ------------------------------------------- |
| AKP03 v2 framing upgrade                                             | HIGH (unblocks 13 devices)      | LOW-MEDIUM                | **P1**                                      |
| `EncoderReleased` proper core event                                  | MEDIUM                          | LOW                       | **P1**                                      |
| AKP05E 0x3004 → `functional`                                          | HIGH (user's device)            | LOW (after framing)       | **P1**                                      |
| AK980 PRO RGB mode + sleep-timer → `partial`                         | HIGH (headline feature)         | LOW                       | **P1**                                      |
| 8K mouse probe → `partial`/`functional`                              | HIGH (user's device)            | MEDIUM                    | **P1**                                      |
| AKP05E 0x3004 `clock` demotion                                        | LOW (honesty)                   | LOW                       | **P1**                                      |
| AK980 PRO `clock` demotion OR TftClockWidget reroute (arch decision) | MEDIUM (honesty)                | LOW (decision) + variable | **P1**                                      |
| Microdia 0c45:7016 catalogue entry (probed)                          | LOW                             | LOW                       | **P1**                                      |
| Real-hardware UI verifications back-fill (v1.1 deferred)             | MEDIUM (closes audit debt)      | LOW                       | **P1**                                      |
| AK980 PRO TFT image upload                                           | MEDIUM (enables TftClockWidget) | MEDIUM                    | **P2**                                      |
| AKP05E 0x3004 per-knob RGB via `SETLB`                                | LOW-MEDIUM                      | LOW                       | **P2**                                      |
| AKP03 animated GIF on keys                                           | MEDIUM                          | MEDIUM                    | **P2**                                      |
| 8K mouse OSD overlay                                                 | MEDIUM                          | MEDIUM                    | **P2**                                      |
| AK980 PRO battery readback                                           | MEDIUM                          | MEDIUM (capture needed)   | **P2**                                      |
| AK980 PRO per-key RGB                                                | HIGH (advertised feature)       | HIGH (capture needed)     | **P3**                                      |
| AK980 PRO macro upload                                               | HIGH (advertised feature)       | HIGH (capture needed)     | **P3**                                      |
| AK980 PRO layer-switching                                            | HIGH (advertised feature)       | MEDIUM-HIGH (capture)     | **P3**                                      |
| Microdia 0c45:7016 paired-device protocol mapping                    | LOW (until paired device known) | HIGH                      | **P3** (blocked on paired-device discovery) |
| TftClockWidget                                                       | MEDIUM                          | MEDIUM                    | **P3** (post-TFT-upload)                    |

**Priority key:**

- **P1**: v1.2 must-have. Either advances a device promotion path or closes audit debt.
- **P2**: v1.2.x patch series; schedule if time permits.
- **P3**: Future milestones; blocked on captures or downstream features.

______________________________________________________________________

## Competitor Feature Analysis

> "Native AJAZZ vendor app" here means the Mirabox/AJAZZ StreamDock desktop binary (vendor-software-inventory.md Finding 2) plus the AJ-series mouse Windows utility (`ajazz-aj199-official-software`); both are observed only at the input/output layer, never disassembled (CLAUDE.md / clean-room rule).

| Feature                                      | Native AJAZZ vendor apps                                               | streamdeck-linux-gui / OpenDeck                                      | TaxMachine ajazz-keyboard-software-linux   | OpenRGB                                     | This project (v1.2 plan)                                                       |
| -------------------------------------------- | ---------------------------------------------------------------------- | -------------------------------------------------------------------- | ------------------------------------------ | ------------------------------------------- | ------------------------------------------------------------------------------ |
| AKP03 family per-key JPEG upload             | Yes                                                                    | Yes (via mirajazz)                                                   | N/A (keyboard project)                     | N/A                                         | **Match** — after v2 framing upgrade                                           |
| AKP03 family encoder + side-button events    | Yes                                                                    | Yes (mirajazz protocol_version 2)                                    | N/A                                        | N/A                                         | **Match** — opcodes confirmed by `opendeck-akp03/src/inputs.rs`                |
| AKP03 family animated GIF on keys            | Yes                                                                    | Partial (still-image only typically)                                 | N/A                                        | N/A                                         | **Differentiator** (v1.2.x) — multi-frame `BAT` send                           |
| AKP03 family per-knob RGB (`SETLB`)          | Yes                                                                    | No                                                                   | N/A                                        | N/A                                         | **Differentiator** (v1.2.x) — adds `rgb` capability to AKP03                   |
| AKP03 family host-settable RTC               | **No** (rendered widget only)                                          | No                                                                   | N/A                                        | N/A                                         | **Honest demote** — no opcode exists, advertising would lie                    |
| AK980 PRO 20 RGB lighting modes              | Yes                                                                    | N/A                                                                  | **Yes (captured by TaxMachine)**           | Unknown — `0c45:8009` not in supported list | **Match** — protocol fully captured                                            |
| AK980 PRO TFT image / GIF upload             | Yes                                                                    | N/A                                                                  | Function exists, chunked-send TODO         | N/A                                         | **Match planned** (v1.2.x) — needs paired-with-hardware capture for chunk size |
| AK980 PRO per-key RGB                        | Yes                                                                    | N/A                                                                  | `// TODO reverse engineer custom RGB data` | N/A                                         | **Future** (v1.3+) — capture required                                          |
| AK980 PRO macros + layers                    | Yes                                                                    | N/A                                                                  | TODO (not yet captured)                    | N/A                                         | **Future** (v1.3+) — capture required                                          |
| 8K mouse DPI + polling-rate + RGB            | Yes                                                                    | N/A                                                                  | N/A                                        | Possibly (`aj_series` may overlap)          | **Match** — protocol already in `aj_series.cpp`, probe-and-confirm             |
| 8K mouse OSD on DPI change                   | Possibly                                                               | N/A                                                                  | N/A                                        | No                                          | **Differentiator** (v1.2.x)                                                    |
| Hot-plug correctness across all four devices | Mixed (Windows USB stack handles most; Linux requires app cooperation) | Per-device                                                           | Per-device                                 | Yes for supported devices                   | **Match** (v1.1 already shipped HOTPLUG-01..07)                                |
| Plugin sandbox for third-party scripts       | No (vendor apps don't expose a plugin SDK on this scale)               | OpenDeck has plugins; streamdeck-linux-gui has limited extensibility | No                                         | Plugin API is C++ only                      | **Differentiator** (already shipped in v1.0 as SEC-003 plugin host)            |
| Honest maturity tier per device              | No (sells everything as "fully supported")                             | No                                                                   | No (single-device project)                 | No (binary supported / not)                 | **Differentiator** (already shipped in v1.1 as DEVICES-01..04)                 |

______________________________________________________________________

## Sources

**Live hardware (read 2026-05-15 from user's machine):**

- `lsusb -d 0300:3004 -v`, `0c45:8009 -v`, `3151:5007 -v`, `0c45:7016 -v` — descriptor metadata for the 4 connected devices
- `/sys/bus/usb/devices/{1-13.3.4,1-10,1-13.2,1-13.1.2}/` — USB topology, confirming `0c45:7016` is NOT a secondary interface of AK980 PRO
- `udevadm info -a -n /dev/hidraw{5,6,11..19}` — interface-to-hidraw mapping per device

**OSS reverse-engineering corpora (clean-room references):**

- [4ndv/opendeck-akp03](https://github.com/4ndv/opendeck-akp03) — Rust OpenDeck plugin for AKP03 / Mirabox N3 family; canonical AKP03 protocol-version-2 vs -3 distinction; input-report action codes confirmed
- [4ndv/mirajazz](https://github.com/4ndv/mirajazz) — underlying Rust crate (`v0.12.1`) with the full opcode table (`DIS`/`LIG`/`BAT`/`CLE`/`STP`/`HAN`/`CONNECT`/`SETLB`/`LBLIG`) and 1024-byte framing for protocol-version 2/3 devices. **Confirms no `setTime` / RTC / clock opcode exists for the AKP03 family.**
- [TaxMachine/ajazz-keyboard-software-linux](https://github.com/TaxMachine/ajazz-keyboard-software-linux) — clean-room Linux app for AK820 Pro at `0c45:8009`; captures the AK980 PRO-family RGB / sleep / image protocol (3-stage `START`/cmd/`FINISH`, 64-byte feature reports, 20 lighting modes, brightness 0..5 not 0..100). Includes a `hidfilter.py` pcap parser for replicating the capture methodology
- [SonixQMK/Mechanical-Keyboard-Database](https://github.com/SonixQMK/Mechanical-Keyboard-Database) — SONiX SN32-family keyboard catalogue. Does NOT list AK980 PRO or AK820 Pro specifically (the database focuses on QMK-portable hardware); does confirm AJAZZ AK33 / K620T / K870T are SN32F248-based.
- [SonixQMK/qmk_firmware (sn32_openrgb branch)](https://github.com/SonixQMK/qmk_firmware/tree/sn32_openrgb) — OpenRGB-compatible firmware variant for SN32 chips (informational; the AK980 PRO does NOT run this firmware, but the chip family overlaps)
- [SonixQMK/sonix_dumper](https://github.com/SonixQMK/sonix_dumper) — firmware-dump tool for SN32F248/SN32F268; can be used for deeper protocol probes if needed (high-effort, low-priority)
- In-tree `docs/research/vendor-protocol-notes.md` Finding 16 — open-web Stream Dock catalogue normalisation, identifies the AKP03 family wire format with `[ajazz-sdk]` + `[opendeck-akp03]` + `[companion]` citations
- In-tree `docs/protocols/streamdeck/akp03.md` — existing AKP03 family protocol doc, including action-code table and the `0x0300:0x3004` annotation
- In-tree `docs/protocols/keyboard/proprietary.md` — existing proprietary-keyboard backend doc covering `0x3151:0x4024-0x4029` family; the AK980 PRO `0x04` Report ID + `0x08-0x0D` opcode set documented there overlaps partially with TaxMachine's AK820 Pro capture (both use Report ID `0x04`, but TaxMachine's cmd 0x13 effect-mode is NOT in the in-tree doc)

**Vendor product pages (input layer only — feature claims and physical layout):**

- [AJAZZ AK980 Mechanical Keyboard with Color Screen 98 Keys RGB (ajazzbrand.com)](https://ajazzbrand.com/products/ajazz-ak980-keyboard) — feature claim list, "TFT screen shows time / connection mode / battery / custom GIF"
- [MechLands AJAZZ AK980 V2 Mechanical Gaming Keyboard with Display Screen (Amazon listing)](https://www.amazon.com/MechLands-AK980-Mechanical-Keyboard-Swappable/dp/B0DF7STD5F) — confirms 8000 mAh, BT/USB-C/2.4 GHz tri-mode, hot-swappable, "linear switch + knob"
- [AJAZZ AJ159 APEX 8000Hz Gaming Mouse (ajazzbrand.com)](https://ajazzbrand.com/products/ajazz-aj159-apex-mouse) — sibling SKU to the 8K mouse; PAW3950 sensor, dock-housed receiver, 8K polling 2.4 GHz / 1K wired
- [Mirabox N3 product page (referenced via opendeck-akp03 README)](https://github.com/4ndv/opendeck-akp03) — AKP03 family physical specs

**USB ID databases (PID identification):**

- [usb-ids.gowdy.us — Microdia 0c45](https://usb-ids.gowdy.us/read/UD/0c45) — confirms `0c45:7016` is NOT registered in the public USB-ID DB; nearby `0c45:70xx` PIDs are temperature sensors / foot switches (not input devices), confirming the database has substantial gaps in the wireless-receiver range
- [DeviceHunt — Microdia vendor 0C45](https://devicehunt.com/view/type/usb/vendor/0C45) — secondary database, also no `7016` entry
- [Sagacious 2013 — 0c45:7000 Microdia receiver for iPazzPort KP-810-18BR](https://himself.wordpress.com/2013/05/14/0c457000-microdia-receiver-for-ipazzPort-commander-bluetooth-kp-810-18br/) — establishes the Microdia `7xxx` pattern as wireless-receiver dongles

**UX / notification design references (carried from v1.1 research):**

- [NN/g — Indicators, Validations, and Notifications](https://www.nngroup.com/articles/indicators-validations-notifications/) — non-modal disconnect / per-row capability badge UX
- [Carbon Design System — Notification pattern](https://carbondesignsystem.com/patterns/notification-pattern/) — toast/glyph guidance for the honest `NotImplemented` and `clock`-demoted surfaces

**Internal sources (canonical for this project):**

- `.planning/PROJECT.md` — v1.2 milestone goal, target features, Out of Scope
- `.planning/MILESTONES.md` — v1.1 deferred-items section
- `.planning/milestones/v1.1-research/FEATURES.md` — competitive analysis frame inherited here
- `docs/_data/devices.yaml` lines 264-305 — current `akp05e` + `ak980pro` + `ajazz_24g_8k` catalogue entries
- `src/devices/streamdeck/src/{register.cpp,akp03_protocol.hpp,akp03.cpp}` — existing AKP03 backend; the v2 framing migration target
- `src/devices/keyboard/src/register.cpp` — `0c45:8009` already wired to ProprietaryKeyboard with `hasClock=true`; demotion target
- `src/devices/mouse/src/{register.cpp,aj_series.cpp}` — `3151:5007` routed through aj_series; probe-and-confirm target
- CLAUDE.md `## Hard rules` — no `wine`, no system-level mutations, COD-031 boundary
- User-memory `project_wire_format_convention.md` — `Profile::deviceCodename` ⇄ `"device"` wire key for any future profile changes

______________________________________________________________________

*Feature research for: AJAZZ Control Center v1.2 Connected-Device Capability Parity*
*Researched: 2026-05-15*
