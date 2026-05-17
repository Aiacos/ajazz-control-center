# AJ-series mouse — vendor device & SKU matrix

> Companion to `aj_series_vendor.md`. This document enumerates every
> VID:PID the vendor `iot_driver.exe` recognises and every renderer-side
> SKU entry that maps to a friendly `displayName`. Use this when adding
> new PIDs to our `src/devices/mouse/src/register.cpp` promotion path.

---

## 1 — All VID:PID pairs the iot_driver lists

Source: the `support` table embedded verbatim in `iot_driver.exe`
(`iot_driver_strings.txt:1027–1241`). The table is a Rust slice
literal that the driver iterates during USB hot-plug to decide whether
to take ownership of a device. Every device the renderer can talk to
**must** appear in this list — even keyboards, mice, dongles and BLE
bridges from rebrands.

### 1.1 Manufacturer-VID coverage

| VID | Manufacturer (per USB-IF) | Devices in table | AJAZZ rebrand? |
|----:|---------------------------|-----------------:|----------------|
| `0x006f` | Unknown | 1 | — |
| `0x0461` | Primax | 2 | (Primax-built rebrand) |
| `0x046a` | Cherry | 2 | (Cherry-built rebrand) |
| `0x05ac` | Apple | 1 | (Apple Trackpad — diagnostic only?) |
| `0x0687` | (unassigned) | 1 BLE | unknown rebrand |
| `0x0738` | Mad Catz | 2 | (Mad Catz rebrand) |
| `0x0c45` | Microdia | 1 boot | (AK980 PRO family — shared with our backend) |
| `0x117f` | (unassigned) | 2 | unknown |
| `0x25a7` | Areson | 5 | (Areson rebrand) |
| `0x25aa` | Sino Wealth | 8 + 4 boot | (most KZZI / DAXA SKUs) |
| `0x2717` | Xiaomi | 1 | (Xiaomi rebrand) |
| `0x3151` | **AJAZZ** (SONiX-VID) | 41 active + 19 boot | **yes — primary AJAZZ VID** |
| `0x331a` | (unassigned) | 1 | unknown |
| `0x342d` | (unassigned) | 30 | (rongyuan / IO1.1PRO rebrand) |
| `0x347a` | (unassigned) | 6 | (akko rebrand) |
| `0x374a` | (unassigned) | 22 | (VKMS / Valkyrie rebrand) |
| `0x3794` | (unassigned) | 17 | (MagneticJade rebrand) |

### 1.2 Full AJAZZ-VID `0x3151` table

Format: `pid` / `interface_number` / flags. `usage_page = 0xffff` is
the vendor configuration interface; `0xff01` is the bootloader;
`0xff55`/`0xff66` are BLE.

| PID | iface | flags | Role |
|----:|------:|-------|------|
| `0x4001` | -1 | bootloader (`0xff01`) | OTA boot (legacy) |
| `0x4002` | 0 | keyboard usage | keyboard |
| `0x4003` | 0 | keyboard usage | keyboard |
| `0x4007` | 2 | vendor | mouse/keyboard |
| `0x4008` | 2 | vendor | mouse/keyboard |
| `0x400a` | -1 | bootloader | OTA boot |
| `0x400b` | 2 | vendor | device |
| `0x400d` | -1 | bootloader | OTA boot |
| `0x4010` | 2 | vendor | device |
| `0x4011` | 2 | vendor, **dongle_common** | shared dongle (keyboard + mouse) |
| `0x4012` | -1 | BLE (`0xff66`) | BLE keyboard |
| `0x4013` | -1 | BLE (`0xff55`) | BLE mouse |
| `0x4014` | 2 | vendor, **dongle_common** | shared dongle |
| `0x4015` | 2 | vendor | device |
| `0x4016` | -1 | bootloader | OTA boot |
| `0x4017` | 2 | vendor, **dongle_common** | shared dongle |
| `0x4018` | 2 | vendor | device |
| `0x4019` | -1 | bootloader | OTA boot |
| `0x401a` | -1 | vendor on -1 (rare) | unknown / debugger |
| `0x401b` | 2 | vendor | device |
| `0x401C` | -1 | BLE (`0xff55`) | BLE mouse |
| `0x401D` | -1 | BLE (`0xff66`) | BLE keyboard |
| `0x401e` | 2 | vendor | device |
| `0x401F` | -1 | BLE (`0xff55`) | BLE mouse |
| `0x4020` | -1 | BLE (`0xff66`) | BLE keyboard |
| `0x4021` | 2 | vendor, **dongle_common** | shared dongle |
| `0x4022` | 2 | vendor | device |
| `0x4023` | -1 | BLE (`0xff55`) | BLE mouse |
| `0x4024` | -1 | BLE (`0xff66`) | BLE keyboard |
| `0x4025` | -1 | bootloader | OTA boot |
| `0x4026` | 2 | vendor | **AJ159 APEX wireless (2.4G receiver path)** |
| `0x4027` | 2 | vendor, **dongle_common** | AJ159 APEX dongle |
| `0x4028` | -1 | BLE (`0xff55`) | AJ159 APEX BLE mode |
| `0x4029` | -1 | bootloader | OTA boot |
| `0x402a` | 2 | vendor | device |
| `0x402b` | -1 | bootloader | OTA boot |
| `0x402C` | -1 | BLE (`0xff55`) | BLE mouse |
| `0x402d` | 2 | vendor, **dongle_common** | shared dongle |
| `0x402e` | -1 | bootloader | OTA boot |
| `0x402f` | 2 | vendor | device |
| `0x4035` | 2 | vendor | device |
| `0x4036` | -1 | bootloader | OTA boot |
| `0x4037` | -1 | BLE (`0xff55`) | BLE mouse |
| `0x4038` | 2 | vendor, **dongle_common** | shared dongle |
| `0x4039` | -1 | bootloader | OTA boot |
| `0x403a` | 0 | vendor on iface 0 (rare) | device |
| `0x403b` | -1 | bootloader | OTA boot |
| `0x403c` | 2 | vendor | device |
| `0x5001` | -1 | bootloader | OTA boot |
| `0x5002` | 2 | vendor | device |
| `0x5004` | -1 | BLE (`0xff55`) | BLE mouse |
| `0x5006` | 2 | vendor, **dongle_common** | shared dongle |
| `0x5007` | 2 | vendor, **dongle_common** | `ajazz_24g_8k` per our `register.cpp` |
| `0x5008` | 2 | vendor, **dongle_common** | **AJ159 APEX wired** |
| `0x5009` | 2 | vendor | device |
| `0x500a` | 2 | vendor | device |
| `0x5020` | 2 | vendor, **dongle_common** | shared dongle |
| `0x5021` | 2 | vendor | device |
| `0x5024` | -1 | bootloader | OTA boot |
| `0x5025` | 2 | vendor | device |
| `0x5026` | 2 | vendor, **dongle_common** | shared dongle |
| `0x5027` | -1 | BLE (`0xff55`) | BLE mouse |
| `0x5028` | -1 | bootloader | OTA boot |
| `0x5029` | 2 | vendor | device |
| `0x502A` | -1 | bootloader | OTA boot |
| `0x502B` | 2 | vendor, **dongle_common** | shared dongle |
| `0x502C` | 2 | vendor | device |
| `0x502D` | 2 | vendor | device |
| `0x502E` | 2 | vendor | device |
| `0x502F` | 2 | vendor | device |
| `0x5030` | 2 | vendor | device |
| `0x5031` | 2 | vendor, **dongle_common** | shared dongle |
| `0x5032` | 2 | vendor | device |
| `0x5033` | 2 | vendor | device |
| `0x5035` | 2 | vendor | device |
| `0x5036` | -1 | bootloader | OTA boot |
| `0x5037` | 2 | vendor, **dongle_common** | shared dongle |
| `0x5038` | 2 | vendor, **dongle_common** | shared dongle |
| `0x5039` | -1 | bootloader | OTA boot |

> **"dongle_common"** flag means the same dongle is shared between a
> keyboard and a mouse on the same 2.4-GHz channel. The iot_driver
> emits two `Device` entries for one physical dongle in that case —
> one keyboard, one mouse — and the renderer's `gm.some(…
> mouseId === -id)` lookup at `js:726799` is how it selects which
> sub-device the current HID write should target.

> **"ble"** flag means the device shows up as a BLE GATT profile
> (usage_page `0xff55` for mouse, `0xff66` for keyboard). BLE devices
> use a separate transport path (`hid_ble.rs`) inside the iot_driver
> rather than `hid_normal.rs`.

> **"bootloader"** flag means the device's USB descriptor reports it
> in OTA mode. Firmware upload uses interface `-1` and usage_page
> `0xff01`. The iot_driver's job here is to discover the device and
> relay raw bytes — the renderer drives the actual upload flow via
> `mledUpgrade()` / `oledUpgrade()`.

---

## 2 — Renderer-side `displayName` ↔ PID mapping (AJAZZ-VID, mouse-class only)

Source: renderer device table at `js:61771–64876` (truncated for
brevity to mouse entries; `type: "mouse"` filter applied).

### AJ159 APEX

| Variant | id | vid | pid | layout | otherSetting |
|---------|----:|-----|----:|--------|--------------|
| wired (PAW3950 sensor) | `-2147485997` | `0x3151` | `0x5008` | `Ki` (8K capable) | `_l` |
| wireless (2.4G) | `2349` | `0x3151` | `0x4026` | `Hi` (1K capped) | `_l` |
| BLE | (paired via `0x4028`) | `0x3151` | `0x4028` | — | — |
| dongle | `0x4027` | `0x3151` | `0x4027` | — | — |

Capabilities (per `Ki` layout at `js:59571`):

```
light.isRgb       : false        // AJ159 has NO per-key RGB (battery LED only)
dpi.count         : 8
dpi.max           : 42000
dpi.min           : 50
dpi.delt          : 50           // step size
reportRate        : [125, 250, 500, 1000, 2000, 4000, 8000]   // Hz (wired only)
                  : [125, 250, 500, 1000]                     // (Hi layout — wireless)
layer             : 8            // onboard profile count
support_onboard   : 2
featureReportByteLength : 65     // 1 report id + 64 body
```

### AJ179 APEX

| Variant | vid:pid | layout |
|---------|---------|--------|
| wired   | `0x3151:0x5008` | `Ki` (same as AJ159 wired) |
| wireless| `0x3151:0x4026` | `Hi` |
| Other dual-mode-3 variants | `id=2360` and `id=2347` | — |

Same capability profile as AJ159 — they share the screen-with-3950
sensor platform.

### AJ179 TITAN

| Variant | vid:pid | layout |
|---------|---------|--------|
| (entries `61201, 61217`) | shares PIDs with AJ179 family | — |

### AJ179 PRO

`js:63425, 63441` — entries `displayName: "AJ179 PRO"`. Same VID.

### AJ139 V2 MAX

`js:62293, 62309` — older Pixart 3395-class device.

### M-series mice (re-brands)

The renderer has many entries like `M92`, `M512`, `M5PRO Max`, `M1 Pro`,
`M1`, `M82XU`, `M600S`, `M701`, `M750`, `M84X`, `M64`, `M94 PRO` etc.
Most use the same `Ki` / `Hi` / `Zi` / `Qi` layouts which all share
the `0x53` / `0x54` omnibus wire format. The differences are:

- DPI count (5..8)
- DPI max (12000..42000)
- Report rate caps (1K..8K)
- RGB capability (per-key vs none vs battery-LED only)

### Branded family map (per `company` field)

| `company` | Display brand | Example SKUs |
|-----------|---------------|--------------|
| `AJAZZMOUSE`        | AJAZZ      | AJ139, AJ159, AJ179 |
| `GamingMouse`       | Generic     | (rare) |
| `AQIRYSmousesoftware`| AQIRYS    | (gaming brand) |
| `rongyuan`          | RONGYUAN    | G16-B, IO1.1ProMax v7.1 |
| `VKMS`              | Valkyrie    | VK M1 Pro, VK M2 Pro, Valkyrie M1 |
| `炫光`              | Xuanguang   | Chinese-market brand |
| `akko`              | AKKO        | various |
| `IO1.1PRO`          | IO1.1       | io11promax |
| `KEB` (variant)     | KEB         | KEB M607 PRO |

The current branding selected at launch comes from
`resources/app/CurrentCompany.json`:

```json
{"currentCompany": "AJAZZMOUSE"}
```

Other rebrands would ship the same `iot_driver.exe` with a different
top-level JSON file plus a different `company/company_<NAME>/`
directory of icons / wallpapers.

---

## 3 — Per-SKU capability matrix (mouse only, AJAZZ-VID, recommended for promotion)

For our `register.cpp` we should promote the AJAZZ-VID mouse PIDs
with vendor-matched wire format. Listed in priority order:

| Priority | PID family | Display | Sensor / max DPI | Poll rate cap | RGB | Screen | OTA target |
|---------:|------------|---------|------------------|--------------:|-----|--------|------------|
| **P0** | `0x5008` wired | AJ159 APEX | PAW3950 / 42K | 8K | no (battery-LED only) | TFT | `0x5024` boot |
| P0 | `0x4026` 2.4G + `0x4027` dongle | AJ159 APEX wireless | PAW3950 / 42K | 1K (2.4G) | no | TFT | `0x4025` boot |
| P1 | `0x5007` | (`ajazz_24g_8k` — current placeholder) | (assume PAW3395 / 26K) | 8K | unknown | unknown | unknown |
| P1 | `0x5006` | dongle | shared | — | — | — | — |
| P2 | `0x4022`, `0x4035`, `0x402f` etc. | (M-series rebrands) | varies | varies | varies | usually no | — |
| P2 | `0x4012`/`0x4013` BLE | AJAZZ keyboard/mouse BLE | varies | 1K (BLE) | varies | varies | — |

> **`0x5007` is what we currently have in `register.cpp` as
> `ajazz_24g_8k`.** Per the iot_driver table, it's a dongle_common
> device — it shares a USB dongle with a keyboard. Promotion-wise it's
> probably an older AJ-series wireless variant. Until we get USBPcap
> evidence it's safest to assume the same wire format as AJ159 family
> (same omnibus packets), but the firmware may not implement all
> opcodes (e.g. no TFT screen on this model).

---

## 4 — Other-VID rebrands worth knowing about

These are physically AJAZZ-built devices that ship under partner VIDs.
The renderer-side `displayName` is the user-facing brand; the wire
format is the same `0x53` / `0x54` mouse-class extension.

| VID | Likely brand | Notable PIDs |
|----:|--------------|--------------|
| `0x25aa` (Sino Wealth) | KZZI / DAXA | `0x2005..0x2008`, `0x4005..0x4008`, `0x8002` |
| `0x342d` | rongyuan / GM series | `0xe3d7..0xe3fd` (entire `0xe3xx` block) |
| `0x347a` | akko | `0xa301..0xa306` |
| `0x374a` | VKMS / Valkyrie | `0xa204..0xa238` (22 entries) |
| `0x3794` | MagneticJade | `0xb310..0xb33b` (17 entries) |
| `0x331a` | unknown | `0x5023` only |

The renderer device-table entries for these VIDs have `company` fields
that pick the appropriate branding folder. Crucially **they all share
the same wire format** as the AJAZZ-VID mice (because they all use the
same iot_driver binary).

---

## 5 — Dongle pairing model

A `dongle_common = true` dongle exposes **two HID children** to the
host:

1. A keyboard interface (usage_page `0xffff`, usage `0x1`, interface 1).
2. A mouse interface (usage_page `0xffff`, usage `0x2`, interface 2).

The iot_driver fires two `Device` events (one per role) and the
renderer correlates them via the `DangleCommon` proto message
(`js:50957`). The `keyboardId` and `mouseId` fields on `DangleCommon`
let the renderer route HID writes through the right child even though
both share the same `vid:pid`.

For our backend the relevant flow is:

1. On `IDevice::open()` for a dongle PID, query if both children exist.
2. If yes, expose the device as **two separate `IDevice`s** — one
   `IKeyboardCapable`, one `IMouseCapable`.
3. When writing, use the same `/dev/hidraw*` path (the dongle has one
   parent USB device but multiple HID interfaces); select the
   interface number based on the role.

---

## 6 — `is24` boolean (proto `Device.is24`)

The `Device.is24` boolean (`js:50798`) is `true` when the device sits
behind a 2.4-GHz dongle. Wired devices set it `false`. BLE devices
have their own `usage_page` (`0xff55` / `0xff66`) so they don't need
this flag.

Used to:

- Cap the polling rate to 1K for `is24 = true` devices on UI (some
  dongles physically can't sustain 8K).
- Show the dongle / battery icon in the renderer's device picker.
- Decide whether to issue `changeWirelessLoopStatus(stop)` before
  firmware OTA (only meaningful for 2.4-GHz devices).

---

## 7 — Code corrections required

### 7.1 `src/devices/mouse/src/register.cpp`

Currently registers:

| Constant | VID | PID |
|----------|----:|----:|
| `ajazz_24g_8k` | `0x3151` | `0x5007` |

**Promote** (after the wire-format rewrite is verified against an
AJ159):

| Constant | VID | PID | Source |
|----------|----:|----:|--------|
| `aj159_apex_wired`    | `0x3151` | `0x5008` | iot_driver `support` table + renderer `id=-2147485997` |
| `aj159_apex_24g`      | `0x3151` | `0x4026` | iot_driver `support` table + renderer `id=2349` |
| `aj159_apex_dongle`   | `0x3151` | `0x4027` | dongle_common — exposes as both kbd + mouse children |
| `aj159_apex_ble`      | `0x3151` | `0x4028` | BLE — defer (out of scope for v1.x) |
| `aj179_apex_wired`    | `0x3151` | `0x5008` (id 2360) | shared with AJ159 — same wire fmt |
| `aj179_apex_24g`      | `0x3151` | `0x4026` (id 2347) | shared with AJ159 |

> **Do NOT promote** any of the partner-VID SKUs (`0x342d`, `0x374a`
> etc.) without explicit consent — they're potentially trademarked
> rebrands, and shipping AJAZZ-style support for them in our binary
> without permission could be construed as enabling counterfeit
> support.

### 7.2 `src/devices/mouse/include/ajazz/mouse/capabilities.hpp`

Add per-SKU capability struct:

```cpp
struct AjSeriesSkuCapabilities {
    std::uint8_t  dpiStageCount;    // 5..8
    std::uint16_t dpiMin, dpiMax;   // 50..42000 typical
    std::uint16_t dpiStep;          // typically 50
    bool          hasRgb;           // per-key RGB
    bool          hasScreen;        // TFT LCD presence
    std::vector<std::uint16_t> pollRateHzList;   // {125,250,500,1000} or {125..8000}
    std::uint8_t  onboardProfileCount;  // 8 for all AJ-series
    std::uint8_t  onboardMacroCount;    // 20 = MACROMAX
    bool          isWireless;
    bool          isBle;
};

// And a per-PID lookup:
static constexpr AjSeriesSkuCapabilities kAj159ApexWired = {
    .dpiStageCount = 8, .dpiMin = 50, .dpiMax = 42000, .dpiStep = 50,
    .hasRgb = false, .hasScreen = true,
    .pollRateHzList = {125, 250, 500, 1000, 2000, 4000, 8000},
    .onboardProfileCount = 8, .onboardMacroCount = 20,
    .isWireless = false, .isBle = false,
};
static constexpr AjSeriesSkuCapabilities kAj159Apex24g = {
    .dpiStageCount = 8, .dpiMin = 50, .dpiMax = 42000, .dpiStep = 50,
    .hasRgb = false, .hasScreen = true,
    .pollRateHzList = {125, 250, 500, 1000},   // 2.4G capped
    .onboardProfileCount = 8, .onboardMacroCount = 20,
    .isWireless = true, .isBle = false,
};
```

### 7.3 Catch2 tests

```cpp
TEST_CASE("AJ159 wired registered with correct capabilities",
          "[mouse][register]") {
    auto reg = makeDeviceRegistry();
    auto desc = reg.lookup(0x3151, 0x5008);
    REQUIRE(desc.has_value());
    CHECK(desc->codename == "aj159_apex_wired");
    CHECK(desc->capabilities.dpiStageCount == 8);
    CHECK(desc->capabilities.pollRateHzList.back() == 8000);
}

TEST_CASE("AJ159 2.4G capped at 1000 Hz",
          "[mouse][register]") {
    auto reg = makeDeviceRegistry();
    auto desc = reg.lookup(0x3151, 0x4026);
    REQUIRE(desc.has_value());
    CHECK(desc->codename == "aj159_apex_24g");
    CHECK(desc->capabilities.pollRateHzList.back() == 1000);
}
```

---

## 8 — References

| Subject | File:Line |
|---------|-----------|
| Full `support` PID table | `iot_driver_strings.txt:1027–1241` |
| `boot` (OTA) PID table   | `iot_driver_strings.txt:1203–1237` |
| `vender` table (interface-1 reads) | `iot_driver_strings.txt:1238–1240` |
| AJ159 APEX wired SKU entry | `dist/static/js/main_beautified.js:61771–61785` |
| AJ159 APEX 2.4G SKU entry  | `dist/static/js/main_beautified.js:61787–61801` |
| `Ki` layout (8K capable)   | `dist/static/js/main_beautified.js:59571` |
| `Hi` layout (1K capped)    | `dist/static/js/main_beautified.js:59558` |
| `_l` otherSetting (AJ159)  | `dist/static/js/main_beautified.js:60105–60125` |
| `Device.is24` proto field  | `dist/static/js/main_beautified.js:50798` |
| `DangleCommon` proto       | `dist/static/js/main_beautified.js:50957` |
| `featureReportByteLength: 65` | `dist/static/js/main_beautified.js:61781, 61797` |
