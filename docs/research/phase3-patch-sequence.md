# Phase 3 patch sequence — concrete atomic commits for the
# post-2026-05-17 deep-RE batch

> Companion to
> [`clean-reimplementation-roadmap.md`](./clean-reimplementation-roadmap.md)
> §12. Where the roadmap catalogues findings, **this doc sequences the
> atomic commits** that execute the corrections + new features. Each
> entry is one independently-revertable commit.
>
> **Risk tiers (ordering within tier is by dependency):**
>
> - **LOW** (P3.1–P3.7) — additive only; pure protocol-header / register
>   / log changes with no behaviour delta on existing call paths.
> - **MEDIUM** (P3.8–P3.11) — touches behaviour but blast radius is
>   bounded to a single device family or a new code path users opt into.
> - **HIGH** (P3.12–P3.16) — large rewrites or device-behaviour
>   semantics changes; require hardware witness or explicit USB capture
>   before promotion past `scaffolded`.
> - **P1 deferred** (P3.17–P3.20) — substantial features deferred to a
>   dedicated milestone after the P0 batch ships.
>
> **Status at session start (2026-05-17 evening)**: §11.1–§11.4 of the
> roadmap landed in this session (`00acf5e`, `eea50a5`, `24c0965`,
> `ef9597b`). §11.5 (P0.5 mouse rewrite) is reclassified as **P3.12**
> below; new findings from the deep RE pass were merged into the
> sequence.
>
> **Test naming convention** uses Catch2 tags in `[brackets]`. CTest
> filter via `-R '\[tag\]'`. ASCII only per
> [CLAUDE.md](../../CLAUDE.md) "Cross-platform build strictness".

______________________________________________________________________

## Index of commits

| # | Title | Risk | Hours |
|---|-------|------|------:|
| **LOW** | | | |
| P3.1 | `feat(keyboard): document AK980 PRO settings-batch byte map (0x07 0x10) constants` | LOW | 1 |
| P3.2 | `feat(keyboard): add AK980 PRO TFT bulk-upload opcode constants (0x72)` | LOW | 1 |
| P3.3 | `feat(keyboard): add AK980 PRO opcode constants 0x15 0x04, 0x19 0x04, 0x0C 0x10` | LOW | 1 |
| P3.4 | `feat(streamdeck): register V25 codenames + Mirabox N4 Pro/N6 variants` | LOW | 2 |
| P3.5 | `feat(mouse): register AJ159 APEX wired + 2.4G + dongle PIDs + partner-VID rows` | LOW | 3 |
| P3.6 | `feat(keyboard): emit CMD_FINISH 0xF0 on every AK980 PRO commit envelope` | LOW | 3 |
| P3.7 | `feat(streamdeck): propagate VER + ULEND to AKP153 + AKP815` | LOW | 3 |
| **MEDIUM** | | | |
| P3.8 | `fix(streamdeck): document WinUSB framing offset delta in akp05_protocol.hpp` | MEDIUM | 2 |
| P3.9 | `feat(keyboard): implement AK980 PRO TFT image_pipeline via bulk 0x72 path` | MEDIUM | 16 |
| P3.10 | `feat(keyboard): expose AK980 PRO 20 firmware RGB modes (opcode 0x13)` | MEDIUM | 8 |
| P3.11 | `feat(keyboard): implement AK980 PRO per-key RGB write (opcode 0x20 0x04)` | MEDIUM | 12 |
| **HIGH** | | | |
| P3.12 | `refactor(mouse): rewrite aj_series.cpp wire format per AJ159 vendor RE` | HIGH | 40 |
| P3.13 | `feat(mouse): expand devices.yaml with AJ159 family + maturity adjustments` | HIGH | 6 |
| P3.14 | `feat(streamdeck): AKP05 single-instance + VER probe + isOld293Version handshake` | HIGH | 8 |
| P3.15 | `feat(keyboard): implement AK980 PRO 9 host-side lighting effects (CustomLightMode)` | HIGH | 32 |
| P3.16 | `feat(host): SDPluginServer Elgato v6 + 26 AJAZZ extensions (loopback-only)` | HIGH | 80 |
| **P1 (deferred to dedicated milestone)** | | | |
| P3.17 | `feat(plugin): plugin-store catalogue model + manifest parser` | P1 | 16 |
| P3.18 | `feat(plugin): bundle + spawn 12 default plugins (sdPlugin loader)` | P1 | 24 |
| P3.19 | `feat(keyboard): AK980 PRO macros (record + assign) with mouse-remap fix` | P1 | 32 |
| P3.20 | `feat(core): IProfileCapable mix-in + ProfileManager + ForegroundAppMonitor` | P1 | 24 |

**Total**: ~314 person-hours (~8 person-weeks) for P0+MEDIUM+HIGH (P3.1
through P3.16). P1 deferred milestone adds ~96 hours (~2.5 weeks).

______________________________________________________________________

## LOW-RISK additive commits (P3.1 through P3.7)

### P3.1 `feat(keyboard): document AK980 PRO settings-batch byte map (0x07 0x10) constants`

**Files touched**:
- `src/devices/keyboard/src/proprietary_protocol.hpp:88` (insert before
  `makeReport`) — add `CmdSettingsBatch = 0x07`, `SettingsBatchSub =
  0x10`, `SettingsBatchTrailerHi = 0xAA`, `SettingsBatchTrailerLo =
  0x55`, plus the byte offsets `kSettingsByteFixed = 5`,
  `kSettingsByteDisableWinKey = 6`, `kSettingsByteDisableAltF4 = 7`,
  `kSettingsByteDisableAltTab = 8` (= checksum slot),
  `kSettingsByteFnSwitch = 9`, `kSettingsByteSleepTime = 10`,
  `kSettingsByteKeyResponseTime = 12`, `kSettingsByteTrailerHi = 18`,
  `kSettingsByteTrailerLo = 19`.
- NO implementation in `.cpp` yet (deferred to a follow-up commit that
  wires the UI).

**Diff size estimate**: ~30 lines header-only.
**Breaking?** No — additive constants only.
**Test coverage**: none required this commit (no code path uses the
constants yet). Tag `[ak980pro-protocol]` for follow-up.
**Dependencies**: none.
**Verification**:
```
cmake --build --preset linux-debug --target ajazz_keyboard
ctest --preset linux-release -R '\[ak980pro-protocol\]'   # zero matches expected
```
**Risk rationale**: header-only constants. Cannot affect any existing
behaviour. Critical foundation for §12.1.1 byte-map correction.
**Citation**:
[`ak980pro_vendor.md` §13.2](../protocols/keyboard/ak980pro_vendor.md).

______________________________________________________________________

### P3.2 `feat(keyboard): add AK980 PRO TFT bulk-upload opcode constants (0x72)`

**Files touched**:
- `src/devices/keyboard/src/proprietary_protocol.hpp:88` — add
  `CmdScreenHeader = 0x7F` (chunked path), `CmdScreenSubBegin = 0x03`,
  `CmdScreenChunkMarker = 0x80`, `CmdScreenBulkBegin = 0x72` (bulk
  path), `CmdScreenSave = 0x02` (alias for CmdSaveRtc but documented
  semantically distinct).
- Also add geometry constants: `kTftWidth = 240`, `kTftHeight = 135`,
  `kTftFrameBytes = 240*135*2`, `kTftChunkPayload = 28`,
  `kTftBulkChunkSize = 4096`, `kTftMaxFrames = 140`, `kTftGifHeader =
  256`, `kTftInterChunkMs = 2`.

**Diff size estimate**: ~20 lines header-only.
**Breaking?** No — additive constants.
**Test coverage**: none required this commit. Tag `[ak980pro-tft]` for
follow-up tests in P3.9.
**Dependencies**: none.
**Verification**: as P3.1.
**Risk rationale**: header-only.
**Citation**:
[`ak980pro_tft_protocol.md` §8.1](../protocols/keyboard/ak980pro_tft_protocol.md).

______________________________________________________________________

### P3.3 `feat(keyboard): add AK980 PRO opcode constants 0x15 0x04, 0x19 0x04, 0x0C 0x10`

**Files touched**:
- `src/devices/keyboard/src/proprietary_protocol.hpp:88` — add wireless
  macro opcodes: `CmdMacroBeginWireless = 0x19`,
  `MacroBeginWirelessSub = 0x04`, `CmdMacroChunkInfoWireless = 0x15`,
  `MacroChunkInfoWirelessSub = 0x04`. Also add LCD-aware time-sync
  alias: `CmdSetTimeLcd = 0x0C`, `CmdSetTimeLcdSub = 0x10`.
- Add comment block above each opcode citing
  [`ak980pro_vendor.md` §13.9 + §13.8](../protocols/keyboard/ak980pro_vendor.md).

**Diff size estimate**: ~15 lines header-only.
**Breaking?** No.
**Test coverage**: none required.
**Dependencies**: none.
**Verification**: as P3.1.
**Risk rationale**: header-only.
**Citation**: `ak980pro_vendor.md` §13.8 + §13.9.

______________________________________________________________________

### P3.4 `feat(streamdeck): register V25 codenames + Mirabox N4 Pro/N6 variants`

**Files touched**:
- `src/devices/streamdeck/src/register.cpp:138` (AKP03 family block) —
  add `(0x6602, 0x1003)` MBox-N3E (rev 1, currently missing); add
  codename strings `AKP03V25`, `AKP03EV25`, `AKP03RV25`, `SD12N3V25`,
  `TS16N3V25`, `VSDN3`, `MBox-N3V25`, `MBox-N3EV25`, `MBox-N3 EV25`,
  `MSD-TWOV25`, `OMNIDIALV25` pointed at the existing `makeAkp03`
  factory (same wire protocol; new codenames for log/UI purposes only).
- `src/devices/streamdeck/src/register.cpp:227` (AKP05 family block) —
  add `AKP05V25`, `AKP05EV25`, `AKP05RV25`, `MBox-N4Pro`,
  `MBox-N4ProE`, `MBox-N6`, `N4Pro`, `N4ProE`, `N4V25`, `MSDPRO`,
  `MSDNEO`, `SD14N4V25`, `TS10N4V25`, `VSDN4`, `VSDN4Pro`, `BRHubN4`,
  `BRHubN4Pro`. PIDs unknown — provisional, log on hot-plug.
- `src/devices/streamdeck/src/register.cpp` end of `registerAll()` —
  add `TS183` codename rebadge for AKP815 family.
- `tests/unit/devices/streamdeck/test_register_codenames.cpp` (new) —
  ~6 cases verifying each new codename resolves to the correct factory.

**Diff size estimate**: ~150 lines code (mostly the register table
expansion) + ~80 lines test.
**Breaking?** No — additive enum / registry extensions.
**Test coverage**: tag `[streamdeck-register]`. 6 cases minimum:
- `RegisterTest::miraboxN3E_oldVendor_0x1003_resolves`
- `RegisterTest::akp03V25_codename_branch`
- `RegisterTest::akp05V25_codename_branch`
- `RegisterTest::miraboxN4Pro_resolves`
- `RegisterTest::miraboxN6_resolves`
- `RegisterTest::ts183_codename_branch_to_akp815`
**Dependencies**: none.
**Verification**:
```
cmake --build --preset linux-debug
ctest --preset linux-release -R '\[streamdeck-register\]'
```
**Risk rationale**: additive enum / registry rows. No existing PID
resolves change. New codenames simply gain a recognised path.
**Citation**:
[`akp_device_matrix.md` §§4-5, §13](../protocols/streamdeck/akp_device_matrix.md).

______________________________________________________________________

### P3.5 `feat(mouse): register AJ159 APEX wired + 2.4G + dongle PIDs + partner-VID rows`

**Files touched**:
- `src/devices/mouse/src/register.cpp:88` (current registry; tiny file
  with just `ajazz_24g_8k` at `0x3151:0x5007`) — add:
  - `aj159_apex_wired` = `0x3151:0x5008`, marked `scaffolded` with
    `notes:` saying "wire format vendor-correct per
    `aj_series_opcode_table.md`; promotion to `partial` requires
    real-device round-trip witness"
  - `aj159_apex_24g` = `0x3151:0x4026`
  - `aj159_apex_dongle` = `0x3151:0x4027` (dongle_common — annotate as
    "shares 2.4G channel with paired keyboard; exposes both kbd + mouse
    children")
  - AJ179 APEX shares the wired + 24g PIDs — annotate as aliases.
- DO NOT add partner-VID SKUs (`0x342d`, `0x374a`, `0x347a`, `0x3794`)
  — trademark risk per
  [`aj_series_device_matrix.md` §7.1 note](../protocols/mouse/aj_series_device_matrix.md).
- `docs/_data/devices.yaml` — add 3 new rows for the AJAZZ-VID
  promotions; honest `feature_summary` (everything `pending` until
  P3.12 lands and a hardware witness occurs).
- `tests/unit/devices/mouse/test_register_aj159.cpp` (new) — ~4 cases
  verifying PID lookups + capability defaults.

**Diff size estimate**: ~120 lines code + ~60 lines test + ~30 lines
yaml.
**Breaking?** No — additive registry rows; existing `0x5007` row
unchanged.
**Test coverage**: tag `[mouse-register]`. 4 cases:
- `RegisterTest::aj159ApexWired_resolves_at_0x5008`
- `RegisterTest::aj159Apex24g_capped_at_1000Hz`
- `RegisterTest::aj159ApexDongle_dongleCommon_flag`
- `RegisterTest::partnerVids_NOT_registered`
**Dependencies**: none.
**Verification**:
```
cmake --build --preset linux-debug
ctest --preset linux-release -R '\[mouse-register\]'
```
**Risk rationale**: additive. The new rows are `scaffolded` so users
get a "device recognised but feature-set unverified" UX rather than no
recognition at all.
**Citation**:
[`aj_series_device_matrix.md` §1.2 + §7.1](../protocols/mouse/aj_series_device_matrix.md).

______________________________________________________________________

### P3.6 `feat(keyboard): emit CMD_FINISH 0xF0 on every AK980 PRO commit envelope`

**Files touched**:
- `src/devices/keyboard/src/proprietary_protocol.hpp:88` — add
  `CmdFinish = 0xF0` constant + `buildCommitFinish()` builder
  declaration.
- `src/devices/keyboard/src/proprietary_keyboard.cpp` (look up actual
  line numbers via `Grep buildSetTimeSave`):
  - In the existing `setTime()` path (after `buildSetTimeSave()` write):
    add `transport.writeFeature(buildCommitFinish())` before returning.
  - In any future commit-envelope path (RGB mode, settings batch, etc.):
    the helper is now available.
- `tests/unit/devices/keyboard/test_ak980pro_setTime.cpp` — extend the
  existing `[clock]`-tagged test to assert a 5th packet emission of
  `[ReportId, 0xF0, …]` after the existing 4-packet sequence. The
  vendor RE shows this packet appears at the end of every multi-packet
  envelope.

**Diff size estimate**: ~40 lines code + ~30 lines test extension.
**Breaking?** TECHNICALLY YES (changes byte count on the wire from 4
packets to 5) but firmware tolerates the additional 0xF0 (vendor sends
it always). Risk: if firmware reads CMD_FINISH and re-locks the RTC,
the visible behaviour could change. Mitigation: hardware witness on
real AK980 PRO before flipping ARCH-05.1 to FINAL.
**Test coverage**: tag `[clock][ak980pro-protocol]`. Existing test
gains 1 new assertion.
**Dependencies**: P3.1, P3.3 (constants).
**Verification**:
```
cmake --build --preset linux-debug
ctest --preset linux-release -R '\[clock\]'
```
**Rollback plan**: revert single commit; the 4-packet envelope was the
known-good state from `17abda4`. Pre-commit hook + regression test
catches any wire-format drift.
**Hardware-witness requirement**: re-run AK980 PRO setTime hardware
witness 2 (TFT clock display) after this lands. If TFT still updates →
behaviour preserved + vendor pattern matched. If TFT stops updating →
rollback immediately + investigate.
**Citation**:
[`ak980pro_vendor.md` §13.7 + §13.11](../protocols/keyboard/ak980pro_vendor.md).

______________________________________________________________________

### P3.7 `feat(streamdeck): propagate VER + ULEND to AKP153 + AKP815`

**Files touched**:
- `src/devices/streamdeck/src/akp153_protocol.hpp` — add `CmdVersion =
  {'V','E','R'}` (3-byte) + `CmdUploadFinished = {'U','L','E','N','D'}`
  (5-byte) + `buildVersionRequest()` / `buildUploadFinished()`
  declarations. Mechanical copy from `akp05_protocol.hpp` / `akp03_protocol.hpp`.
- `src/devices/streamdeck/src/akp815_protocol.hpp` — same.
- `src/devices/streamdeck/src/akp153.cpp` — implement builders;
  `firmwareVersion()` replace `"unknown"` with VER round-trip; emit
  `ULEND` from `flush()` after `STP`.
- `src/devices/streamdeck/src/akp815.cpp` — same pattern.
- `tests/unit/devices/streamdeck/test_akp153_version_ulend.cpp` (new)
  — 2 cases.
- `tests/unit/devices/streamdeck/test_akp815_version_ulend.cpp` (new)
  — 2 cases.

**Diff size estimate**: ~60 lines code + ~80 lines test.
**Breaking?** No — additive opcode emission; firmware ignores VER if
it doesn't understand it. ULEND adds 1 packet per image-burst flush.
**Test coverage**: tag `[streamdeck-protocol]`. 4 cases:
- `Akp153ProtocolTest::open_emitsVerRequest_andCachesResponse`
- `Akp153FlushTest::imageBurst_endsWith_STP_then_ULEND`
- `Akp815ProtocolTest::open_emitsVerRequest_andCachesResponse`
- `Akp815FlushTest::imageBurst_endsWith_STP_then_ULEND`
**Dependencies**: none (carry-over from §11.3).
**Verification**:
```
cmake --build --preset linux-debug
ctest --preset linux-release -R '\[streamdeck-protocol\]'
```
**Risk rationale**: mechanical copy of an already-landed (`24c0965`)
pattern. Same firmware family.
**Citation**:
[`akp05_vendor.md` §2 + §11.3 of roadmap](../protocols/streamdeck/akp05_vendor.md).

______________________________________________________________________

## MEDIUM-RISK commits (P3.8 through P3.11)

### P3.8 `fix(streamdeck): document WinUSB framing offset delta in akp05_protocol.hpp`

**Files touched**:
- `src/devices/streamdeck/src/akp05_protocol.hpp` (look up the existing
  framing comment block) — add a `// WINUSB FRAMING NOTE:` paragraph
  documenting that the WinUSB transport (when it lands) puts `CRT` at
  bytes 0..2, opcode at byte 5, payload at byte 8 — one byte further
  left than the hidapi layout. Cite
  [`akp05_init_sequence.md` §3.4](../protocols/streamdeck/akp05_init_sequence.md).
- No code change — comment-only.

**Diff size estimate**: ~15 lines comment.
**Breaking?** No.
**Test coverage**: none.
**Dependencies**: none.
**Verification**: build only.
**Risk rationale**: comment-only. Critical for any future WinUSB
implementer to not silently misalign opcodes by one byte.
**Citation**: `akp05_init_sequence.md` §3.4.

______________________________________________________________________

### P3.9 `feat(keyboard): implement AK980 PRO TFT image_pipeline via bulk 0x72 path`

**Files touched**:
- `src/devices/streamdeck/src/image_pipeline.cpp` (existing) →
  generalise: move RGBA → format-X encoder into a new shared static lib
  `ajazz_imaging` under `src/core/imaging/`. JPEG output stays for
  Stream Dock; new RGB565 output (big-endian, 240×135) for AK980 PRO
  TFT.
- `src/devices/keyboard/include/ajazz/keyboard/screen_uploader.hpp`
  (new) — `class ScreenUploader : public QObject` with
  `uploadGif(QString path)` slot + `progress(int percent)` /
  `uploadFinished(bool ok)` signals. Decodes GIF via `QMovie` to ≤140
  frames; builds per-frame delay header; uploads via bulk 0x72 path.
- `src/devices/keyboard/src/screen_uploader.cpp` (new) — implementation:
  1. CMD_START (0x18 0x04, FEATURE 65 B)
  2. CMD_SCREEN_BEGIN_BULK (0x72 0x00, FEATURE 65 B, byte 4..5 = total
     4 KB chunks LE, byte 3 = LCD-index + 1)
  3. bulk body via OUTPUT 4097 B writes (`hid_write` chunking)
  4. CMD_SAVE (0x02 0x04, FEATURE 65 B)
  5. CMD_FINISH (0xF0 0x04, FEATURE 65 B — per P3.6)

  Fallback: if `0x72` FEATURE write returns error → fall back to chunked
  0x7F + 0x80|n path (slow but works).
- `src/core/include/ajazz/core/capabilities.hpp` — extend `IDisplayCapable`
  to apply to keyboards (currently Stream-Dock-only). Add helper
  `setGifAnimation(QList<QImage>, QList<int> delaysMs)`.
- `tests/unit/devices/keyboard/test_tft_packet_builder.cpp` (new) — ~6
  cases per `ak980pro_tft_protocol.md` §8.2:
  - `TftHeaderTest::bulkBegin_emits_0x72_with_LE_chunkCount`
  - `TftHeaderTest::chunkIndex0_encodesCorrectly`
  - `TftHeaderTest::chunkIndex2314_lastOfOneFrame_encodes`
  - `TftHeaderTest::chunkIndex324099_lastOf140FrameGif_encodes`
  - `TftRgb565Test::pureRed_encodesAs_0xF800_bigEndian`
  - `TftBandwidthTest::bulkPath_envelope_emits_5_envelope_packets`

**Diff size estimate**: ~400 lines code (worker + ajazz_imaging
refactor) + ~250 lines test.
**Breaking?** No — entirely new code path. Existing `image_pipeline.cpp`
is unchanged in behaviour (just moved + extended).
**Test coverage**: tag `[ak980pro-tft]`. 6 cases.
**Dependencies**: P3.1, P3.2, P3.3 (constants), P3.6 (CMD_FINISH).
**Verification**:
```
cmake --build --preset linux-debug --target ajazz_keyboard ajazz_imaging
ctest --preset linux-release -R '\[ak980pro-tft\]'
```
**Hardware-witness requirement**: upload a known-pattern GIF (e.g.,
test image with red pixel at (0,0), green at (1,0), blue at (2,0)) to
a real AK980 PRO. Verify TFT shows the colors in the right order +
position. If colors swap → byte order swap needed (see
`ak980pro_tft_protocol.md` §6).
**Risk rationale**: NEW code path; firmware should accept the bulk
path because the vendor uses it. If 0x72 fails → fallback to chunked
path kicks in automatically.
**Citation**:
[`ak980pro_tft_protocol.md` §§3-5](../protocols/keyboard/ak980pro_tft_protocol.md).

______________________________________________________________________

### P3.10 `feat(keyboard): expose AK980 PRO 20 firmware RGB modes (opcode 0x13)`

**Files touched**:
- `src/devices/keyboard/src/proprietary_protocol.hpp` — add `CmdSetRgbMode
  = 0x13` + `buildSetRgbMode(uint8 modeId, Rgb tint, bool rainbow, uint8
  brightness, uint8 speed, uint8 direction)` declaration.
- New file `src/devices/keyboard/include/ajazz/keyboard/ak980_lighting.hpp`
  — strong-typed `enum class AK980LightingMode : uint8_t` with all 20
  values (Static = 0x00, SingleOn = 0x01, Glittering = 0x02, Breath =
  0x03, Spectrum = 0x04, Outward = 0x05, Scrolling = 0x06, Explode =
  0x07, Launch = 0x08, Ripples = 0x09, Flowing = 0x0A, Pulsating = 0x0B,
  Tilt = 0x0C, Shuttle = 0x0D, … LedOff = 0x13). Use `Q_NAMESPACE` +
  `Q_ENUM_NS` so QML can iterate.
- `src/devices/keyboard/src/proprietary_keyboard.cpp` — implement
  builder; expose `setLightingMode(AK980LightingMode mode, Rgb tint,
  …)` method.
- Envelope per [`ak980pro_vendor.md` §13.7](../protocols/keyboard/ak980pro_vendor.md):
  CMD_START 0x18 → CMD_MODE_BEGIN 0x13 → CMD_MODE_DATA (mode_id @
  byte 0, RGB @ 1..3, rainbow @ 8, brightness @ 9, speed @ 10, direction
  @ 11, 0xAA 0x55 trailer @ 14..15) → CMD_SAVE 0x02 → CMD_FINISH 0xF0
  (5-step envelope).
- `tests/unit/devices/keyboard/test_ak980pro_rgb_mode.cpp` (new) — ~5
  cases:
  - `Ak980LightingTest::staticMode_emits_5packet_envelope`
  - `Ak980LightingTest::breathMode_RGB_at_bytes_1_2_3`
  - `Ak980LightingTest::trailerAtBytes_14_15`
  - `Ak980LightingTest::ledOffMode_modeId_0x13`
  - `Ak980LightingTest::rainbowFlag_at_byte8`

**Diff size estimate**: ~200 lines code + ~150 lines test.
**Breaking?** No — new feature.
**Test coverage**: tag `[ak980pro-lighting]`.
**Dependencies**: P3.1, P3.6.
**Verification**:
```
cmake --build --preset linux-debug
ctest --preset linux-release -R '\[ak980pro-lighting\]'
```
**Hardware-witness requirement**: cycle each of 20 modes on a real
AK980 PRO and verify visible animation matches the mode name.
**Risk rationale**: NEW feature; no existing call path affected. If
firmware rejects a mode ID → backend logs warning + continues. UI
shows modes by name; user picks visually.
**Citation**: `ak980pro_vendor.md` §3.4 + §13.7.

______________________________________________________________________

### P3.11 `feat(keyboard): implement AK980 PRO per-key RGB write (opcode 0x20 0x04)`

**Files touched**:
- `src/devices/keyboard/src/proprietary_protocol.hpp` — add per-key RGB
  constants per
  [`ak980pro_perkey_rgb_protocol.md` §6.2](../protocols/keyboard/ak980pro_perkey_rgb_protocol.md):
  - `kCmdPerKeyRgbWrite = 0x20`, `kPerKeyRgbSub = 0x04`
  - `kCmdPerKeyRgbReadback = 0xF5`, `kPerKeyReadbackWiredSub = 0x03`,
    `kPerKeyReadbackWirelessSub = 0x09`
  - `kPerKeyModeWired = 0x03`, `kPerKeyModeWireless = 0x08`
  - `kPerKeyWiredBlobSize = 0xC0` (192), `kPerKeyWirelessBlobSize =
    0x200` (512)
  - `kPerKeyWiredChunkCount = 3`, `kPerKeyWirelessChunkCount = 8` (NOT
    6 — corrects §2.8 of roadmap per §12.1.2)
- New file `src/devices/keyboard/include/ajazz/keyboard/perkey_rgb.hpp`
  — `class PerKeyRgb : public QObject` with `uploadColors(const
  std::vector<LedColor>&, bool isWireless)` + signals.
- `src/devices/keyboard/src/perkey_rgb.cpp` (new) — 3-packet envelope:
  1. Header packet (`[0x20, 0x04, …, mode@byte8]`, 33 B OUTPUT via
     `FUN_0044f0c0`-equivalent path → our `transport.writeFeature(…
     0x41)` chunked)
  2. RGB blob (raw 192/512 bytes, NO opcode prefix, FEATURE 64-byte
     slices via chunked path — `hid_send_feature_report` per chunk)
  3. CMD_SAVE `[0x02, 0x04, …]` 33 B
- `src/devices/keyboard/src/proprietary_keyboard.cpp` — wire up
  `PerKeyRgb` instance; expose `setPerKeyRgb(std::span<Rgb const>
  colors)` capability.
- `tests/unit/devices/keyboard/test_ak980pro_perkey_rgb.cpp` (new) —
  per
  [`ak980pro_perkey_rgb_protocol.md` §6.2](../protocols/keyboard/ak980pro_perkey_rgb_protocol.md)
  — 7 critical tests:
  - `PerKeyRgbTest::wiredBlob_is_exactly_192_bytes_for_192_LEDs_monochrome`
  - `PerKeyRgbTest::wirelessBlob_is_512_bytes_with_reserved_R_G_B_per_LED`
  - `PerKeyRgbTest::wirelessBlob_respects_light_index_addressing`
  - `PerKeyRgbTest::wiredWriteHeader_modeByte_at_offset8_is_0x03`
  - `PerKeyRgbTest::wirelessWriteHeader_modeByte_at_offset8_is_0x08`
  - `PerKeyRgbTest::wirelessReadbackHeader_chunkCount_0x09_at_offsets_2_and_8`
  - `PerKeyRgbTest::wirelessReadbackDecoding_extracts_R_G_B_from_bytes_1_2_3`

**Diff size estimate**: ~350 lines code + ~200 lines test.
**Breaking?** No — new capability.
**Test coverage**: tag `[ak980pro-perkey-rgb]`.
**Dependencies**: P3.1, P3.6.
**Verification**:
```
cmake --build --preset linux-debug
ctest --preset linux-release -R '\[ak980pro-perkey-rgb\]'
```
**Hardware-witness requirement**: upload a sentinel pattern (LED #0 =
red, LED #5 = green, LED #10 = blue) and visually verify on the real
AK980 PRO. Specifically test on WIRELESS (4-byte-per-LED format is
where most encoding errors live).
**Risk rationale**: NEW capability path. Wired path is monochromatic
only (firmware limitation, not a bug we introduce — see
`ak980pro_perkey_rgb_protocol.md` §3.1) — UI must warn user when wired
that per-key RGB is intensity-only.
**Citation**: `ak980pro_perkey_rgb_protocol.md` §§1-3.

______________________________________________________________________

## HIGH-RISK commits (P3.12 through P3.16)

### P3.12 `refactor(mouse): rewrite aj_series.cpp wire format per AJ159 vendor RE`

**Files touched** (this is the biggest commit in the entire sequence):
- `src/devices/mouse/src/aj_series.cpp` (340 lines today, ~600 lines
  after) — wholesale rewrite per
  [`aj_series_opcode_table.md` §6.1](../protocols/mouse/aj_series_opcode_table.md):
  - **Line 48** (was `kCmdCommit = 0x50`): DELETE — vendor has no
    commit step (already removed in `00acf5e`).
  - **Lines 86–94** (current `CommandId` enum with all-wrong opcodes):
    REPLACE with `enum class FeaCmd : std::uint8_t { GetRev = 0x80,
    SetReport = 0x04, SetProfile = 0x05, SetLedParam = 0x07,
    MouseSetKeyMatrix = 0x50, MouseSetFnMatrix = 0x51, MouseSetOption0
    = 0x53, MouseSetOption1 = 0x54, SetMacroSimple = 0x16, SetReset =
    0x02 };`
  - **Line 127** (`& 0xff`): CHANGE to `& 0x7F` (BIT7 — UNBLOCKED by
    §12.1.11 98-site renderer census).
  - **Line 180** (`dpiStageCount() = 6`): CHANGE to `8`.
  - **Lines 217–223** (`setPollRateHz` writes uint16 BE): REPLACE with
    `_RateToNum` constexpr lookup table: `{125→0x08, 250→0x04, 500→0x02,
    1000→0x01, 2000→0x84, 4000→0x82, 8000→0x81}`. Opcode `0x04`. Payload:
    `pkt[1]=profile`, `pkt[2]=rateCode`. Default profile=0.
  - **Lines 227–231** (`setLiftOffDistanceMm` opcode 0x23): DELETE.
    LOD becomes byte 52 of the 0x53 omnibus packet (see
    `AjSeriesOptionPacket` below).
  - **Lines 233–243** (`setButtonBinding` opcode 0x24, payload at byte
    4): REPLACE with opcode `0x50`. Payload structure: `pkt[1]=profile`,
    `pkt[2]=button`, `pkt[8..11]=action`. (The original was offset by 4
    bytes — silent no-op until now.)
  - **Lines 245–252** (`batteryPercent` opcode 0x40): REPLACE with
    `return std::nullopt` for wired SKUs; for wireless, return cached
    value updated by hot-plug callback that watches the dongle's
    battery (Linux:
    `/sys/class/power_supply/hid-<vid>:<pid>.*/capacity`).
  - **Lines 259–270** (`setRgbStatic` / `setRgbEffect` opcode 0x30):
    REPLACE with opcode `0x07` 8-byte payload via `LightSettingToBuffer`
    equivalent — see `aj_series_opcode_table.md` §3.5 byte layout.
  - **Lines 273–277** (`setRgbBrightness` opcode 0x30 sub 0x02): DELETE.
    Brightness is byte 3 of the 8-byte light packet.
  - **Lines 287–298** (`uploadDpiStage` opcode 0x21 per-stage): DELETE.
    Replace with `setMouseOption1(stages, activeIdx)` that writes the
    full 8-stage table atomically via opcode `0x54` (see
    `aj_series_opcode_table.md` §3.10). 8th stage colour B-byte
    collides with checksum — UI grey-out per §12.1.12.
  - **NEW** helper struct `AjSeriesOptionPacket` (per
    `aj_series_opcode_table.md` §6.2) — 64-byte omnibus packet builder
    for opcode 0x53 covering profile, poll rate, debounce, lift-off,
    sensitivity, angle-snap, sleep timer, battery LED, charging LED in
    a single transaction (matches vendor "one save button" UX).
  - **NEW** `firmwareVersion()` impl via opcode 0x80, parses uint16-LE
    at response bytes 1..2.
  - **NEW** `factoryReset()` impl via opcode 0x02.
  - **CRITICAL TRANSPORT FIX**: replace every `transport.writeFeature(…)`
    call site on the mouse path with `transport.write(…)` (interrupt-
    OUT, NOT SET_REPORT). The original §11.5 plan got this wrong; see
    §12.1.10.
- `src/devices/mouse/include/ajazz/mouse/capabilities.hpp` — add
  `AjSeriesSkuCapabilities` per
  [`aj_series_device_matrix.md` §7.2](../protocols/mouse/aj_series_device_matrix.md);
  per-PID lookup table.
- `tests/unit/devices/mouse/test_aj_series_wire_format.cpp` (new, ~400
  lines) — byte-level assertions per
  [`aj_series_opcode_table.md` §6.3](../protocols/mouse/aj_series_opcode_table.md).
  Required cases (8+):
  - `AjWireTest::setActiveDpiStage_emits_0x54_with_correct_active_byte`
  - `AjWireTest::setPollRateHz_8000_emits_byte2_0x81`
  - `AjWireTest::setPollRateHz_full_table_4000_2000_1000_500_250_125`
  - `AjWireTest::setButtonBinding_emits_0x50_with_action_at_byte_8_11`
  - `AjWireTest::setRgbStatic_emits_0x07_type1_brightness_byte3_RGB_5_6_7`
  - `AjWireTest::checksum_is_BIT7_sum_AND_0x7F_of_bytes_1_62`
  - `AjWireTest::setMouseOption0_LOD_byte52_sensitivity_byte50_51`
  - `AjWireTest::getFirmwareVersion_emits_0x80_parses_uint16LE_byte_1_2`
  - `AjWireTest::no_standalone_battery_opcode_emitted_on_writeFeature`
    (regression guard for the §1.1 corruption)
  - `AjWireTest::setMacro_emits_0x16_with_chunk_index_lastChunkFlag_byte4`

**Diff size estimate**: ~600 lines code rewrite + ~400 lines test.
**Breaking?** YES — every mouse opcode changes. Every existing call
path emits different bytes on the wire. BUT existing call paths are
known to be silently no-op or corruption (per §1.1–§1.3 of roadmap);
the rewrite changes "silently broken" to "potentially correct, pending
hardware witness".
**Test coverage**: tag `[mouse][aj_series][wire][checksum][safety]`.
~10 cases.
**Dependencies**: P3.5 (registered PIDs).
**Verification**:
```
cmake --build --preset linux-debug
ctest --preset linux-release -R '\[mouse\]'
```
**Hardware-witness requirement**: USB capture on a real AJ159 APEX
(0x3151:0x5008) of:
- `setPollRateHz(8000)` → first byte should be `0x05` (Report ID), then
  `0x04` (opcode), then `0x00` (profile), then `0x81` (rate code).
- `setActiveDpiStage(3)` → first byte `0x05`, then `0x54`, then `0x00`
  (profile), `0x03` (active), `0x08` (count), then 8 LE DPI values at
  bytes 8..23.
- `setButtonBinding(5, mouseRight)` → first byte `0x05`, then `0x50`,
  then `0x00`, then `0x05` (button idx), zeros to byte 7, then 4-byte
  action at bytes 8..11.
**Rollback plan**: the rewrite lives behind a feature flag
`AJAZZ_AJ_SERIES_WIRE_REWRITE` (CMake option, default `OFF` for one
release, then default `ON` after hardware witness). If hardware witness
fails, flip flag back to `OFF`; the existing safety-guard path from
`00acf5e` keeps users protected.
**Risk rationale**: HIGHEST risk in the sequence. Wire format
changes are the load-bearing change for the mouse backend. BUT the
status quo IS corruption-on-save per §1.1 — the existing code is more
dangerous than the rewrite.
**Honesty gate**: commit message body must explicitly state "wire
format is now vendor-correct per `aj_series_opcode_table.md` but
untested against real hardware; promotion `scaffolded → partial`
requires real-device witness per Hardware-witness requirement above".
**Citation**:
[`aj_series_opcode_table.md` §6.1 + §6.3](../protocols/mouse/aj_series_opcode_table.md) +
[`aj_series_vendor.md` "Other corrections"](../protocols/mouse/aj_series_vendor.md).

______________________________________________________________________

### P3.13 `feat(mouse): expand devices.yaml with AJ159 family + maturity adjustments`

**Files touched**:
- `docs/_data/devices.yaml`:
  - `ajazz_24g_8k` → keep `scaffolded` (registered as P3.5; behaviour
    still pending real device).
  - `aj159_apex_wired` → bump from `scaffolded` (just landed in P3.5)
    to `scaffolded with feature_summary.works` populated as the
    rewrite's wire format completes. Hardware witness flips to
    `partial`.
  - `aj159_apex_24g` → same pattern.
  - `aj159_apex_dongle` → same pattern.
  - Add `notes:` per row citing
    [`aj_series_device_matrix.md` §3](../protocols/mouse/aj_series_device_matrix.md)
    capability matrix (DPI count, polling rate cap, RGB, screen
    presence, OTA target).
- No source code change.

**Diff size estimate**: ~60 lines yaml.
**Breaking?** No — yaml-only.
**Test coverage**: existing `[devices-yaml]` schema validator.
**Dependencies**: P3.5, P3.12.
**Verification**: build + schema test.
**Risk rationale**: yaml-only; existing `[devices-yaml]` test catches
schema violations.
**Citation**: `aj_series_device_matrix.md` §3.

______________________________________________________________________

### P3.14 `feat(streamdeck): AKP05 single-instance + VER probe + isOld293Version handshake`

**Files touched** (per
[`akp05_init_sequence.md` §10](../protocols/streamdeck/akp05_init_sequence.md)):
- `src/host/main/src/main.cpp` (new file, ~80 lines) — Qt 6
  single-instance via `QLocalServer::listen("AjazzControlCenter_<userId>")`;
  fall-back `QLocalSocket::connectToServer()` with argv handoff if
  listening fails; reject `--allow-network-host` flag by default
  (force LocalHost binding everywhere).
- `src/devices/streamdeck/src/akp05.cpp` `Akp05Device::open` — at
  re-acquire-after-DFU, add 2-second backoff before first VER query
  (vendor pattern per
  [`akp_dfu_protocol.md` §9.4](../protocols/streamdeck/akp_dfu_protocol.md)).
- `src/devices/streamdeck/src/akp153.cpp` — add `isOld293Version()`
  helper that issues `hid_get_input_report` (Linux:
  `HIDIOCGFEATURE`) and gates the V1-protocol path for legacy AKP153
  firmware.
- `src/host/persistence/src/registry_paths.hpp` (new file, ~50 lines)
  — registry-key map from
  [`akp05_init_sequence.md` §4](../protocols/streamdeck/akp05_init_sequence.md);
  per-device prefs gated behind a feature flag (we do NOT ship the
  same registry path as the vendor; per CLAUDE.md "No system-level
  mutations" rule).
- `tests/unit/host/test_single_instance.cpp` (new) — 2 cases.
- `tests/unit/devices/streamdeck/test_akp153_old293.cpp` (new) — 2
  cases.

**Diff size estimate**: ~250 lines code + ~150 lines test.
**Breaking?** No — additive.
**Test coverage**: tags `[single-instance]`, `[streamdeck-protocol]`.
**Dependencies**: P3.7.
**Verification**: build + ctest.
**Hardware-witness requirement**: AKP05 reopen after DFU mode + AKP153
legacy firmware test (if such hardware available).
**Risk rationale**: MEDIUM blast — touches main.cpp (currently does
not exist) + new isOld293Version branch. The branch is gated; legacy
AKP153 hardware is rare.
**Citation**: `akp05_init_sequence.md` §10.

______________________________________________________________________

### P3.15 `feat(keyboard): implement AK980 PRO 9 host-side lighting effects (CustomLightMode)`

**Files touched** (per
[`ak980pro_mui_dll.md` §3 + §6](../protocols/keyboard/ak980pro_mui_dll.md)):
- `src/devices/keyboard/lighting/` (new directory) — house the
  `LightingEffectsService` Qt6 service.
- `src/devices/keyboard/include/ajazz/keyboard/lighting/effects.hpp`
  (new) — interface for the 9 effect compositors:
  - `Starlight` (InitLightStar)
  - `Fluttering` (InitLightRain)
  - `ColorfulFountain` (InitFlowerEffect)
  - `DynamicBreathing` (InitBreathEffect)
  - `RainbowWave` (InitSpringEffect)
  - `FollowingCurrents` (InitVerticalEffect)
  - `PeakRevolving` (InitRollEffect)
  - `StaticOnRotate` (InitLightRotate)
  - `MusicReactive` (FFT-driven, optional opt-in)
- `src/devices/keyboard/src/lighting/effects.cpp` (new, ~800 lines) —
  9 effect compositors as pure C++ classes that consume a
  `LightingMatrix` model (144 LEDs × ARGB) and emit cooked RGB frames
  at 60 fps. **Derive lookup tables from first principles** (per
  `ak980pro_mui_dll.md` §3 "These can be re-derived from first
  principles for our impl — no need to copy the data") — we do NOT
  copy the vendor's `DAT_100af***` tables.
- `src/devices/keyboard/src/lighting/lighting_service.cpp` (new) — QML
  singleton + `QTimer` @ 16.67 ms (60 fps) + per-effect compositor
  dispatch + upload via `PerKeyRgb::uploadColors` from P3.11.
- New QML `LightingEffectsPicker.qml` — populated from the strong-typed
  effect enum.
- `tests/unit/devices/keyboard/test_lighting_effects.cpp` (new) —
  snapshot tests against per-effect golden frames at t=0 / t=1s / t=5s.
  ~9 cases (1 per effect).

**Diff size estimate**: ~1500 lines code + ~400 lines test.
**Breaking?** No — entirely new subsystem.
**Test coverage**: tag `[ak980pro-host-lighting]`. Snapshot-based; the
golden frames are committed as fixtures.
**Dependencies**: P3.10, P3.11.
**Verification**:
```
cmake --build --preset linux-debug
ctest --preset linux-release -R '\[ak980pro-host-lighting\]'
```
**Hardware-witness requirement**: visual verification of each effect
on a real AK980 PRO. Effect names should match the user's expectation
(starlight should look like stars, etc.).
**Rollback plan**: feature flag `AJAZZ_HOST_LIGHTING_EFFECTS` (CMake
option). Default `OFF` for one release; flip after positive hardware
witness.
**Risk rationale**: NEW subsystem with substantial new code. Worst
case: effect compositor produces wrong frames → wrong colors on the
keyboard → cosmetic only, no data loss.
**Citation**: `ak980pro_mui_dll.md` §3 + §6.

______________________________________________________________________

### P3.16 `feat(host): SDPluginServer Elgato v6 + 26 AJAZZ extensions (loopback-only)`

**Files touched** (per
[`akp_plugin_sdk.md` §9](../protocols/streamdeck/akp_plugin_sdk.md)):
- `src/host/plugin-host/` (new module, ~1500 lines):
  - `sd_plugin_server.{hpp,cpp}` — `QWebSocketServer` on
    `QHostAddress::LocalHost` (NOT `Any` — see §12.1.14 / anti-feature
    §4.16). Random free port via `QTcpServer::listen(LocalHost, 0)`
    then `serverPort()`.
  - `sd_plugin_protocol.{hpp,cpp}` — implement the 13 standard Elgato
    events + 26 AJAZZ-specific actions tabulated in
    [`akp_plugin_sdk.md` §4.3 + §4.4](../protocols/streamdeck/akp_plugin_sdk.md).
  - `plugin_manifest.{hpp,cpp}` — parse the schema in
    [`akp_plugin_sdk.md` §2](../protocols/streamdeck/akp_plugin_sdk.md)
    including AJAZZ extensions (`IsK1Pro`, `RunAsAdministrator`,
    `FSize`/`FFamily`, `Nodejs.Version`, `PUUID`). PRIVATE-linked
    `nlohmann::json` (COD-031 boundary).
  - `node_runner.{hpp,cpp}` — spawn user-side Node.js (we do NOT
    bundle node20 — detect system node ≥ 20 and reject otherwise).
    CLI: `<node> <codePath> -port <p> -pluginUUID <u> -registerEvent
    registerPlugin -info <info>`.
  - `auth.{hpp,cpp}` — implement salt/challenge per
    [`akp_plugin_sdk.md` §4.5](../protocols/streamdeck/akp_plugin_sdk.md);
    SHA-256 of `password + salt`; reject after 5 failed attempts.
  - `cef_replacement.{hpp,cpp}` — replace QCefView with
    `QWebEngineView` + `QWebChannel`. Map the `cefQuery` JS function
    to a thin polyfill that delegates to the QWebChannel bridge per
    [`akp_plugin_sdk.md` §8](../protocols/streamdeck/akp_plugin_sdk.md).
  - `CMakeLists.txt` — link `Qt6::WebSockets`, `Qt6::WebEngineQuick`,
    `Qt6::WebChannelQuick`, `nlohmann_json` (PRIVATE only — COD-031).
- `resources/sdpi.css` — bundle the Elgato standard CSS file (also
  available under MIT license from elgatosf/streamdeck-sdk on GitHub
  — verify license + add NOTICE entry).
- Compatibility shim: map `connectMiraBoxSDSocket(...)` →
  `connectElgatoStreamDeckSocket(...)` so AJAZZ-only plugins load on
  our host without code changes. Implement as a JS pre-injection in
  our QWebEngineView replacement (see
  [`akp_plugin_sdk.md` §9 last row](../protocols/streamdeck/akp_plugin_sdk.md)).
- Tests (`tests/unit/host/plugin-host/`) — ~12 cases per
  `akp_plugin_sdk.md` §9 table.

**Diff size estimate**: ~1500 lines production code + ~500 lines test.
**Breaking?** No — entirely new module.
**Test coverage**: tag `[plugin-host][sd-plugin-server]`.
**Dependencies**: none (independent subsystem). MUST land BEFORE P3.18
(plugin loader).
**Verification**:
```
cmake --build --preset linux-debug --target ajazz_plugin_host
ctest --preset linux-release -R '\[plugin-host\]'
```
**Risk rationale**: HIGH risk because it's a large new module with a
security-sensitive surface (loopback WebSocket, auth, plugin process
spawn). Mitigations:
- LocalHost-only binding (not `Any`) — anti-feature §4.16
- Salt/challenge auth per §4.5
- Manifest signature verification deferred (§4.17 anti-feature) but
  documented + flagged as gap.
**Citation**: `akp_plugin_sdk.md` §§1-9 + `akp05_init_sequence.md` §5.

______________________________________________________________________

## P1 deferred milestone (P3.17 through P3.20)

These commits depend on P3.16 (plugin host) or address features that
are best implemented after the P0 batch ships. Schedule for a
dedicated `v1.3-plugin-foundation` milestone.

### P3.17 `feat(plugin): plugin-store catalogue model + manifest parser`

**Source**:
[`akp_plugin_sdk.md` §6](../protocols/streamdeck/akp_plugin_sdk.md).

Model the 38-field plugin metadata (per `streamdock_exe_strings.txt`
offset 760172..760199: `author`, `avatar`, `cartId`, `collection`,
`dialSupport`, `discountPrice`, `frequency`, `frontPlugin`, `gallery`,
`isAudio`, `isCollection`, `isRecommend`, `isThumbUp`, `news`,
`overview`, `owned`, `price`, `priceRmb`, `productType`, `reason`,
`reply`, `seniorFrequency`, `superFrequency`). Skip cloud
phone-home (`hotspot-oss-bucket.oss-cn-shenzhen.aliyuncs.com`) per
anti-feature §4.11.

Self-hostable plugin index: a JSON manifest at
`https://plugins.ajazz-control-center.org/index.json` (or a user-
configured URL); local cache in `QStandardPaths::AppLocalDataLocation`.

**Diff size estimate**: ~400 lines code + ~200 lines test.
**Dependencies**: P3.16.
**Risk**: P1 (deferred).

______________________________________________________________________

### P3.18 `feat(plugin): bundle + spawn 12 default plugins (sdPlugin loader)`

**Source**:
[`akp_plugin_sdk.md` §1 + §3](../protocols/streamdeck/akp_plugin_sdk.md).

Implement the 12 default plugin loader (memo, myHeadline,
system.monitor, calendar, dateTime, emoji, pictureEmoticons, PR,
switchAudio, time, weather, mkey.calendar). Spawn-via-QProcess
(native/node20) or in-process QWebEngineView (html), per the manifest's
`CodePath` extension.

DO NOT bundle vendor plugins verbatim — write our own plugin or rely on
upstream Elgato Stream Deck SDK plugins (which work due to API
compatibility).

**Diff size estimate**: ~600 lines code + ~300 lines test.
**Dependencies**: P3.16, P3.17.
**Risk**: P1 (deferred).

______________________________________________________________________

### P3.19 `feat(keyboard): AK980 PRO macros (record + assign) with mouse-remap fix`

**Source**:
[`ak980pro_macros_protocol.md` §§3-7](../protocols/keyboard/ak980pro_macros_protocol.md).

Implement:
- 4-byte event format (KeyDown/Up `0xB0`/`0x30`, Delay `0x50`, Mouse
  Down/Up `0x90`/`0x10`).
- Mouse-remap: `1→0x01 (Left), 2→0x04 (Right), 3→0x02 (Middle)` — **NOT
  identity** — see §12.1.9. Dedicated test case for this.
- Wired path: opcode `0x09 0x1C` with 28-byte chunks (OUTPUT 33 B).
- Wireless path: `0x19 0x04` + `0x15 0x04` + bulk body + `0x02 0x04`
  (FEATURE 65 B).
- Macro-to-key assignment: opcode `0x14` (chunked) or `0x23 0x04`
  (bulk) per
  [`ak980pro_macros_protocol.md` §6 + §7](../protocols/keyboard/ak980pro_macros_protocol.md).
- Document device limitation: macros support key + mouse-button events,
  **NOT** mouse-move or wheel events (firmware limitation per §3.3).

`MacroRecorder` QObject hooks `QAbstractNativeEventFilter` (Win),
`evdev` (Linux), `CGEventTap` (macOS).

**Diff size estimate**: ~800 lines code (incl. 3 OS-specific recorders)
+ ~400 lines test.
**Dependencies**: P3.1, P3.3, P3.6.
**Risk**: P1 (deferred to milestone after P0 ships).

______________________________________________________________________

### P3.20 `feat(core): IProfileCapable mix-in + ProfileManager + ForegroundAppMonitor`

**Source**: roadmap §7.

Cross-device profile system + per-app auto-switch. Killer feature
(`t_profile_data.app TEXT` in AK980 PRO schema). Cross-cuts
IProfileCapable across mouse (AJ159, 8 profiles via opcode 0x05),
keyboard (AK980 PRO, 8 profiles host-driven), Stream Dock (host-only
profiles in JSON).

ForegroundAppMonitor: cross-OS abstraction (Win32 `SetWinEventHook`
/ D-Bus / NSWorkspace) — NOT polling per §4.9 anti-feature.

**Diff size estimate**: ~600 lines code + ~200 lines test.
**Dependencies**: P3.12 (mouse rewrite with opcode 0x05), P3.19
(macros — profile triggers macros optionally).
**Risk**: P1 (deferred).

______________________________________________________________________

## Decision matrix

### Contradictions resolution table

| ID | Contradiction | DECISION | RATIONALE |
| -- | ------------- | -------- | --------- |
| C-1 | Settings-batch byte map (§2.7 vs §13.2) | USE §13.2 (P3.1) | Deep RE has stack-offset evidence; no in-tree migration risk |
| C-2 | Per-key RGB chunk count (60 vs 64 bytes) | USE 64-byte chunks (P3.11) | `FUN_0044f0c0` chunking loop traced; 60-byte was wrong opcode lineage |
| C-3 | Report ID 0x00 vs 0x04 (§13.1) | **DEFER** | gohv/KyleBoyer/our impl all work with 0x04 prefix. Capture-validate before changing. Risk > value of theoretical correctness |
| C-4 | Mouse transport: writeFeature vs hid_write | USE `hid_write` (P3.12) | Renderer census proves mouse uses `sendMsg` not `sendRawFeature` |
| C-5 | DPI stages (8 with 8th-stage colour quirk) | EXPOSE 8 stages + grey out 8th colour (P3.12) | Firmware accepts 8 but truncates 8th colour B; UI must reflect |
| C-6 | Stream Dock listener binding (`Any` vs `127.0.0.1`) | LOCALHOST ONLY (P3.16) | Vendor footgun → anti-feature §4.16 |
| C-7 | Stream Dock WinUSB framing offset (1 byte left) | DOCUMENT NOW (P3.8), IMPLEMENT LATER | Only affects WinUSB touch-strip which we don't yet wire |
| C-8 | TFT upload path priority | PROMOTE 0x72 bulk to P0 v1.2 (P3.9) | 143× speedup; 0x7F stays as fallback |
| C-9 | LCD-aware time-sync (0x0C 0x10 vs 0x28) | KEEP 0x28, document 0x0C 0x10 as future | Our 0x28 envelope works; switching opcodes is high-risk for zero UX benefit |
| C-10 | "11 default plugins" vs "12" | UPDATE to 12 (P3.18) | Off-by-one corrected |
| C-11 | Default plugins vs built-in UUIDs | DOCUMENT BOTH (P3.16, P3.18) | Built-in UUIDs handled in-process; plugins spawn subprocess |

### High-risk patch mitigation

| Patch | Mitigation | Rollback plan | Feature flag? |
| ----- | ---------- | ------------- | ------------- |
| **P3.6** (CMD_FINISH 0xF0) | Hardware witness on AK980 PRO TFT clock after landing | Revert single commit | No (additive byte; firmware tolerates) |
| **P3.9** (TFT bulk 0x72) | Fallback to chunked 0x7F path on bulk failure | Revert to chunked-only impl | Yes — `AJAZZ_TFT_BULK_PATH` default `ON` |
| **P3.11** (per-key RGB) | Sentinel-pattern test on real hardware (LED #0 red, #5 green, #10 blue) before flipping `pending → works` | Revert; no other code path uses these constants | No |
| **P3.12** (AJ-series rewrite) | USB capture validation of 3 representative packets BEFORE landing; `AJAZZ_AJ_SERIES_WIRE_REWRITE` flag default `OFF` first release | Flip flag back to `OFF`; existing `00acf5e` safety-guard keeps users protected | YES — REQUIRED |
| **P3.14** (single-instance) | Test on all 3 platforms (Linux/macOS/Windows) before landing | Revert single commit; existing 2nd-instance behaviour was "no enforcement" (benign) | No |
| **P3.15** (host lighting) | Snapshot tests against golden frames; visual verification on real device | Feature flag `AJAZZ_HOST_LIGHTING_EFFECTS` default `OFF` | YES |
| **P3.16** (SDPluginServer) | LocalHost binding enforced; auth challenge required; manifest signature gap documented | Module-level disable via CMake `-DAJAZZ_BUILD_PLUGIN_HOST=OFF` | YES — module-level |

### External blockers (`[needs capture]` items)

| Blocker | What's needed | Who provides | When |
| ------- | ------------- | ------------ | ---- |
| AK980 PRO Report ID 0x00 vs 0x04 validation (C-3) | USB capture of vendor's first 4 bytes of any FEATURE-report opcode | usbipd-win + WSL2 + tshark per `CAPTURING.md` §8.6 | Before any change to `proprietary_protocol.hpp:23` |
| AJ159 APEX wire-format validation (P3.12) | USB capture of 3 packets: `setPollRateHz(8000)`, `setActiveDpiStage(3)`, `setButtonBinding(5, mouseRight)` | usbipd-win + WSL2 against AJAZZ Driver(R) 2.1.94 on Win VM, real AJ159 hardware | Before flipping `AJAZZ_AJ_SERIES_WIRE_REWRITE=ON` |
| AK980 PRO TFT bulk path validation (P3.9) | Hardware witness: upload sentinel GIF, verify visible output | User with real AK980 PRO + bundled GIF test fixture | After P3.9 lands, before `partial → functional` |
| AK980 PRO per-key RGB validation (P3.11) | Hardware witness: sentinel pattern (LED #0 red, #5 green, #10 blue) on WIRELESS specifically | User with real AK980 PRO in 2.4G mode | After P3.11 lands |
| AKP05 ULEND validation (P3.7) | Hardware witness: 10 rapid setKeyImage() calls, verify no firmware freeze | User with real AKP05/N4 | After P3.7 lands |
| AKP05 V25 firmware version detection (P3.14) | One `VER` response sample to disambiguate `293V25` vs `293V3` | User with real AKP05 V25 hardware | Before `isOld293Version()` ships against AKP153 |
| Stream Dock SDK plugin compat smoke test (P3.16) | Load 3 existing Elgato Stream Deck plugins (e.g., Wave Link, OBS Studio, Hue) and verify they connect + render | User-driven testing post-P3.16 | After P3.16 lands |
| AK980 PRO 9 host-side lighting effects visual verification (P3.15) | Visual confirmation each effect matches its name (starlight looks like stars, etc.) | User with real AK980 PRO | After P3.15 lands |
| AK980 PRO macros wireless mouse-remap (P3.19) | Recording test of "Right click" macro → verify the device replays Right click (not Middle click — the 2↔3 swap regression) | User with real AK980 PRO in wireless mode | After P3.19 lands |
| AJ159 APEX 8K poll rate sustained throughput | Verify mouse can deliver 8 K reports/sec without dropping | User with real AJ159 APEX + perf-monitor tool | Post-P3.12 hardware witness |
| AKP05 / AKP03R rev. 2 1024-byte packet size detection (`akp_device_matrix.md` §13 row 325-326) | VER response sample + capture of one image upload | User with real AKP05 V25 / AKP03R rev. 2 | Deferred — currently we send 512-byte packets and firmware tolerates |

______________________________________________________________________

## Cross-cutting refactor opportunities

The deep RE pass surfaced 3 patterns that span multiple commits and
suggest dedicated abstractions:

### CC-1 Shared `AjazzVendorSDK` Qt6 service?

The 9 host-side lighting effects on AK980 PRO (P3.15), the
SDPluginServer host-side rendering (P3.16), and the future AJ159 TFT
widget set (roadmap §3.6) all need:
- Worker-thread compositing pipeline
- 60 fps frame budget tracker
- Cooked-frame → device-specific upload coordinator

**Recommendation**: extract a shared `ajazz_compositing` static lib
under `src/core/compositing/` that the 3 features share. Defer the
extraction until P3.15 lands; refactor at that point with extraction
as a follow-up commit.

### CC-2 `dj_hid_sdk_rs` Rust binary as a shared upstream?

The AJ159 vendor's `iot_driver.exe` strings reveal it was built from
`D:\work\dj_hid_sdk_rs\target\i686-pc-windows-msvc\release\…\driver.rs`
([`aj_series_vendor.md` references §1622](../protocols/mouse/aj_series_vendor.md)).
The same Rust SDK ships across:
- AJAZZ branded mice/keyboards
- KZZI / DAXA / akko / VKMS / rongyuan / MagneticJade / Mad Catz
  rebrands (6 partner VIDs per
  [`aj_series_device_matrix.md` §1.1](../protocols/mouse/aj_series_device_matrix.md))

This means **a single wire-format spec covers ~120 PIDs** across these
brands. Our backend can support partner devices with the same code
once we have explicit consent (or once we determine trademark-safe
labelling).

**Recommendation**: defer until post-v1.3. The `aj159_apex_wired`
family is the priority for v1.2; partner-VID rows can land
incrementally if user demand emerges.

### CC-3 `ITransport` clarity on write() vs writeFeature()

The mouse `hid_write` vs keyboard `hid_send_feature_report` distinction
(per §12.1.5 transport matrix) suggests `ITransport` should have
explicit `write()` (interrupt-OUT) and `writeFeature()` (SET_REPORT)
methods, not just one polymorphic write. Audit current backend code
for ambiguous calls before P3.12 lands.

**Recommendation**: add a P3.0 commit BEFORE P3.12 that:
- Audits every `ITransport::write*` call in mouse + keyboard backends.
- Confirms which path each call should use per `aj_series_vendor.md`
  Other-corrections §1 + `ak980pro_vendor.md` §13.11 transport matrix.
- Documents in `src/core/include/ajazz/core/transport.hpp` which method
  to use for which opcode family.

Estimated 4 hours; renumber subsequent commits if added.

______________________________________________________________________

## Anti-features enumeration (carried + new)

The 15 anti-features from roadmap §4 + 5 new from §12.5:

| # | Anti-feature | Source | Disposition |
| - | ------------ | ------ | ----------- |
| §4.1 | AJ159 gRPC localhost listener | aj_series_vendor.md 510-515 | NEVER — keep in-process `ITransport` |
| §4.2 | AJ159 cloud login | aj_series_vendor.md 24, 494-498 | NEVER — file-system .ajprofile only |
| §4.3 | AJ159 `getWeather` | aj_series_vendor.md 485-489 | NEVER — opt-in user-chosen provider only |
| §4.4 | AJ159 `watchSystemInfo` telemetry | aj_series_vendor.md 490-493 | NEVER — opt-in per widget only |
| §4.5 | AJ159 universal-analytics + node-machine-id | aj_series_vendor.md 499-505 | NEVER — zero analytics SDKs |
| §4.6 | AJ159 recoil-control / rapid-fire | aj_series_vendor.md 231-234 | NEVER — anti-cheat liability (`aj_series_opcode_table.md` §3.14) |
| §4.7 | AK980 PRO encrypted SQLite blob | ak980pro_vendor.md §9 | NEVER — plain JSON via QJsonDocument |
| §4.8 | AK980 PRO HTTP-spawn FirmwareUpdateTool | ak980pro_vendor.md §6 | NEVER — in-app DFU when we ship it |
| §4.9 | AK980 PRO 200 ms foreground-app polling | ak980pro_vendor.md §9 | NEVER — event-driven hooks |
| §4.10 | Stream Dock Alibaba auto-update CDN | akp05_vendor.md §8 | NEVER — Flatpak/MSI/dnf/apt only |
| §4.11 | Stream Dock Alibaba plugin store | akp05_vendor.md §8 | NEVER — self-hostable index |
| §4.12 | Stream Dock global low-level keyboard hook | akp05_vendor.md §1.4 | OPT-IN with prompt; default OFF |
| §4.13 | AK980 PRO custom MUI toolkit | ak980pro_vendor.md §9 | NEVER — Qt6/QML |
| §4.14 | AK980 PRO dynamic LoadLibraryA | ak980pro_vendor.md §1.1 | NEVER — link `hidapi_hidraw` normally |
| §4.15 | AJ159 universal mouse driver dozens-of-SKUs | aj_series_vendor.md 40 | NEVER — AJAZZ-only, no OEM rebrand support |
| **§4.16 NEW** | Stream Dock WebSocket binding to `QHostAddress::Any` | akp05_init_sequence.md §5 + akp_plugin_sdk.md §4.1 | NEVER — bind LocalHost only (P3.16) |
| **§4.17 NEW** | Stream Dock plugin-zip auto-unpack without signature | akp_plugin_sdk.md §6 | DEFER signature verification to dedicated commit; do NOT enable auto-fetch without it |
| **§4.18 NEW** | AK980 PRO `mui.dll` 6 604-export custom toolkit | ak980pro_mui_dll.md §1-§5 | Already covered by §4.13; corroboration only |
| **§4.19 NEW** | Allwinner SoC USB-upgrade (Stream Dock DFU) | akp_dfu_protocol.md §§1-9 | NEVER — detect DFU transition + suspend; never reimplement |
| **§4.20 NEW** | Stream Dock AES-GCM-encrypted firmware with hardcoded key | akp_dfu_protocol.md §4 | NEVER — we don't download or decrypt firmware (already covered by §4.10) |

______________________________________________________________________

## Sequencing check — execution order

```
Week 1 (LOW risk additive):
  P3.1 → P3.2 → P3.3 (constants, header-only)
  P3.4 || P3.5 (device registry expansion, parallel)
  P3.6 (CMD_FINISH; requires P3.1, P3.3)
  P3.7 (VER + ULEND propagation; carry-over)
  + P3.8 (WinUSB framing comment)

Week 2-3 (MEDIUM):
  P3.9 (TFT bulk path; requires P3.1, P3.2, P3.6)
  P3.10 (20 RGB modes; requires P3.1, P3.6)
  P3.11 (per-key RGB; requires P3.1, P3.6)

Week 4-5 (HIGH):
  P3.12 (AJ-series rewrite; biggest commit; behind feature flag)
  P3.13 (devices.yaml update for AJ-series)
  P3.14 (AKP05 init handshake; requires P3.7)

Week 6+ (HIGH continued):
  P3.15 (9 host-side lighting; depends on P3.10 + P3.11)
  P3.16 (SDPluginServer; independent; ~80h)

P1 milestone (separate, post-P0):
  P3.17 → P3.18 → P3.19 → P3.20
```

Total active development for P0 (P3.1 through P3.16): **~211 hours
(~5.5 person-weeks)** assuming 38 productive hours per week. Add
~25% buffer for hardware-witness round trips → **~7 weeks elapsed**.

______________________________________________________________________

*This document drives execution. It will be updated as commits land
(strike through completed entries, add post-mortem notes in case of
rollback or hardware-witness surprise). When all 16 P0 commits land,
declare v1.2.x complete and start a new milestone with P3.17 as
foundation.*
