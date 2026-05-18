# AJ-series mouse — vendor sled KV store schema

> Persistence layout of the AJAZZ vendor driver's `iot_db` directory.
>
> The vendor app stores device configuration in a Rust **sled 0.34.7**
> embedded KV store, accessed over gRPC from the Electron renderer. We do
> **not** plan to copy this design — instead we'll use Qt's
> `QStandardPaths::AppLocalDataLocation` + `QSettings(IniFormat)` and/or
> per-profile `.ajprofile` JSON files (see `aj_series_vendor.md` §UX
> recommendations). This document captures what the vendor stores so we
> know which fields users will expect to survive across sessions.

______________________________________________________________________

## 1 — Storage location

| OS                            | Path                                          |
| ----------------------------- | --------------------------------------------- |
| Windows                       | `%APPDATA%\AJAZZ Driver（R）\iot_db\<table>\` |
| Browser fallback (web-driver) | `./web_driver/iot_db/<table>` (cwd)           |
| Linux (vendor would be)       | `~/.config/AJAZZ Driver（R）/iot_db/<table>/` |

Source: renderer `js:57509`:

```js
var ku = n().join("web_driver", "iot_db");
if (s /* isElectron */) {
  var xu = window.require("@electron/remote").app;
  ku = n().join(xu.getPath("userData"), "iot_db");
}
```

The iot_driver itself opens sled trees at the path the renderer sends
in each gRPC request (`InsertDb.db_path`, `GetItem.db_path`, …). The
absolute path is sent on every call — the Rust daemon doesn't keep a
"current database" notion.

iot_driver default working-directory fallback (`iot_driver_strings.txt:1360`):

```
./iot_db
iot_db
```

… is what gets used if a request supplies a relative path.

______________________________________________________________________

## 2 — Tables (sled tree names)

From renderer `js:57534` `DBPATH` enum:

| Constant                   | Tree name               | Contents (per evidence below)                                                                                                                                         |
| -------------------------- | ----------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `DEVICETYPE`               | `device_type`           | Per-PID device metadata (`displayName`, `dpi.count`, `reportRate`, ...)                                                                                               |
| `CONFIG`                   | `CONFIG`                | Per-device profile-tied configuration (DPI table, polling rate, RGB, button map, sleep times, sensitivity, ...) — the host-side cache of the on-device omnibus packet |
| `MACRO`                    | `macro`                 | 256-byte macro buffers (one per macro slot per device)                                                                                                                |
| `SCREEN`                   | `screen`                | LCD UI configuration (selected widget, weather city, clock format, etc.)                                                                                              |
| `SCREEN_IMAHE` [sic]       | `screen_image`          | LCD wallpaper / animation frames (binary blobs)                                                                                                                       |
| `USER`                     | `user`                  | Cloud login state, machine UUID, last-shared profile, etc.                                                                                                            |
| `CUSTOM_LIGHT`             | `custom_light`          | User-saved RGB scenes (named light presets)                                                                                                                           |
| `CUSTOM_LIGHT_IMAHE` [sic] | `db_custom_light_image` | Per-key wallpaper images for keyboards (n/a for mice)                                                                                                                 |
| `IMG_SHARE`                | `db_img_share`          | Cached "Community Share" — wallpaper images downloaded from the cloud                                                                                                 |
| `LIGHT_SHARE`              | `db_light_share`        | Cached "Community Share" — light presets downloaded from the cloud                                                                                                    |
| `AUDIO`                    | `audio`                 | Recorded audio for media-key macros (rare)                                                                                                                            |
| `GUN`                      | `gun`                   | Recoil-control macros (anti-feature; do **NOT** replicate)                                                                                                            |

The two `_IMAHE` typos in the JS enum are present verbatim and are the
keys the renderer uses — they map to correctly-spelled `db_custom_light_image`
and `screen_image` tree names on disk.

______________________________________________________________________

## 3 — Key schema

The renderer's insert helpers (`js:57537–57556`) show:

```js
var Nu = function(e) {
  var t = JSON.stringify(e);
  return (new TextEncoder).encode(t);   // JSON string → UTF-8 bytes
};

e.insertBufferToDB = function(e, t, r) {            // table, key, valueBytes
  u.setDbpath(n().join(ku, e));                     // absolute path
  u.setKey(Nu(t));                                  // key = JSON-stringified
  u.setValue(new Uint8Array(r));                    // raw bytes
  …
};

e.insertDataToDB = function(e, t, r) {              // table, key, value
  u.setKey(Nu(t));
  u.setValue(Nu(r));                                // value also JSON-stringified
};
```

So **keys are always `JSON.stringify(structuredKey)` encoded as UTF-8**.
The structured-key shape varies per table; common shapes:

- **device-keyed tables** (`CONFIG`, `MACRO`): key is the device's
  composite identifier, typically `{vid: 0x3151, pid: 0x5008, profile: N, slot: M}`
  serialised as JSON. The renderer builds these per call. The
  serialised key is therefore `'{"vid":12625,"pid":20488,"profile":0,"slot":3}'`
  (~50 bytes UTF-8).
- **global tables** (`USER`, `device_type`, `IMG_SHARE`): key is a
  simple string or numeric UUID, e.g. `"current_user"`,
  `"sg9028_dm_8k_rt002"`, `"<uuid-v4>"`.

The vendor's lack of a fixed key-schema contract means every table
read returns a raw `Uint8Array` and parses on the renderer side.

______________________________________________________________________

## 4 — Value schema

Three observed encodings (`js:57514–57530`):

| Encoding                                          | Used by                                                                                                                    |
| ------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------- |
| `JSON.stringify(obj)` → UTF-8 (`Nu()`)            | `insertDataToDB` — every config object                                                                                     |
| Raw `Uint8Array` (binary blob, no encoding)       | `insertBufferToDB` — images, macro buffers                                                                                 |
| `base64` (cached form returned by gRPC `GetItem`) | All reads via `getItemFromDb` — the iot_driver wraps stored bytes in base64 when sending back to the renderer (`js:57579`) |

On read (`getItemFromDb` at `js:57569`):

```js
var t = e;                                          // gRPC reply value
if (typeof t !== "string") t = _u(t);              // char-array → string
var r = du.from(t, "base64");                       // base64 decode
return JSON.parse(r.toString());                    // assume JSON
```

So binary blobs (images, 256-byte macros) round-trip as base64 over the
gRPC wire — the iot_driver stores them raw in sled but base64-encodes
on every read.

______________________________________________________________________

## 5 — Per-table semantics

### 5.1 `device_type`

Persists per-PID device metadata that the renderer needs at startup
before any HID transactions. Mirrors the in-memory table at
`js:61771–64876` (~ 70 entries). Keyed by the device's `name` field
(e.g. `"mouse_pan1080_g62b_screen_1k_8k_3950"`).

Value (JSON):

```json
{
  "id": -2147485997,
  "vid": 12625,
  "pid": 20488,
  "usage": 2,
  "usagePage": 65535,
  "name": "mouse_pan1080_g62b_screen_1k_8k_3950",
  "displayName": "AJ159 APEX",
  "support_onboard": 2,
  "type": "mouse",
  "featureReportByteLength": 65,
  "company": "AJAZZMOUSE",
  "layout": { … },
  "layer": 8,
  "otherSetting": { … }
}
```

Writes: at app startup; updates when the user "favourites" a device
in the multi-device picker.

### 5.2 `CONFIG`

The vendor's host-side mirror of the on-device omnibus packet
(`0x53`). Lets the UI populate the settings screens instantly without
waiting for an HID round-trip.

Key shape (inferred): `{vid, pid, profile, "kind": "option0"}` or
`{vid, pid, profile, "kind": "option1"}`.

Value (JSON, for `option0`):

```json
{
  "profile": 0,
  "rate": 8000,                      // human-readable Hz
  "deBounce": 1,
  "light": {                         // _BufferToLightSetting result
    "type": "LightAlwaysOn",
    "value": 4,                      // brightness 0..6
    "rgb": 16711680,                 // 0xFF0000
    "dazzle": false
  },
  "logoLight": { "type": "LightOff" },
  "sleepTime": {
    "time_bt": 120, "deepTime_bt": 1680,
    "time_24": 120, "deepTime_24": 1680
  },
  "mp": {
    "lightOff": false, "wheelLightOff": false,
    "smooth": true, "ledSelect": false, "powerSaveMode": false,
    "buttonChange": 1, "wheelToButton": 10, "buttonToWheel": 10,
    "xSensitivity": 100, "ySensitivity": 100,
    "liftCutOff": 0, "angelSnap": false
  },
  "batteryColorArr": [                // sic — typo in vendor JS
    { "r": 0, "g": 255, "b": 0 },    // high-charge
    { "r": 255, "g": 0,   "b": 0 }   // low-charge
  ],
  "chargingSwitch": true
}
```

Value (JSON, for `option1`):

```json
{
  "profile": 0,
  "currentDpi": 1,         // active stage index
  "currentDPIMax": 8,
  "dpiArr": [400, 800, 1600, 3200, 6400, 12000, 25000, 42000],
  "DPIColorArr": [
    { "r": 255, "g": 0, "b": 0 },
    { "r": 255, "g": 165, "b": 0 },
    …
  ]
}
```

Writes: every successful `writeFeatureCmd(0x53)` / `writeFeatureCmd(0x54)`
(but **only after** the device acknowledges — the renderer does
optimistic-write-then-cache, not the other way around).

### 5.3 `macro`

Key: `{vid, pid, slot}` (or, since the device-side is keyed by `slot`
only, the renderer may also dedupe per `slot`).

Value (JSON):

```json
{
  "name": "Spray Headshot",
  "repeatCount": 1,
  "macroEvents": [
    { "type": "keyboard", "value": 28, "action": "down" },   // HID usage 0x1C = 'Y'
    { "type": "delay",    "value": 50 },
    { "type": "keyboard", "value": 28, "action": "up" },
    …
    { "type": "mouse_move", "dx": -2, "dy": 4 },
    { "type": "mouse_button", "value": "left", "action": "down" }
  ],
  "createdAt": 1763472000000          // ms since epoch
}
```

Writes: when the user saves a macro in the editor. The device-side
upload (chunked via `0x16`) happens immediately after the sled write.

Size: each macro JSON is ~5–20 KB depending on event count. The
on-device representation is fixed at 256 bytes (5 chunks of 56) and
must be lossy — the renderer's `setMacro` packs by truncating any
events that don't fit.

### 5.4 `screen` / `screen_image`

For SKUs with a TFT LCD (`AJ159 APEX`, `AJ179 APEX/TITAN`,
`mouse_pan1080_g62b_screen_1k_8k_3950`, etc.). `screen` holds widget
config (which UI is shown — clock, weather, CPU info, custom GIF);
`screen_image` holds raw image bytes (PNG / JPEG / 24-bit RGB raw
depending on widget).

Key: `{vid, pid, "kind": "wallpaper"}` for image data, `{vid, pid}`
for widget config.

Value: JSON for widget; raw `Uint8Array` for image (uploaded via
`insertBufferToDB`).

Typical disk: 30–500 KB per image.

### 5.5 `user`

App-wide settings + cloud login state.

Key: `"current_user"` or `"machine_id"` (constant strings).

Value (JSON):

```json
{
  "machineId": "9f3c2eb8-a05a-481e-b3d9-bfd6f8c30a51",   // node-machine-id output
  "token": "<JWT or session token>",
  "username": "...",
  "lastLogin": 1763472000000,
  "language": "en",
  "theme": "dark"
}
```

Writes: on cloud login / logout. The machine-id is computed once by
`node-machine-id` (which calls `wmic csproduct get UUID` on Windows)
and persisted; subsequent runs read it from sled instead of re-querying.

> **Privacy concern**: the machineId is a stable hardware-derived UUID.
> If the cloud endpoints (`api.rongyuan.tech:3814`, `api2.qmk.top:3814`)
> are reachable, that UUID could be cross-correlated with user account
>
> - IP + device PID. Our Qt reimplementation should not use
>   hardware-derived UUIDs for anything — if we ever need a device-scope
>   UUID, generate a random v4 on first run and store it locally.

### 5.6 `custom_light` / `db_custom_light_image`

User-saved RGB scenes ("My RGB"). For mice without per-key RGB
(AJ159 APEX is `isRgb: false`), this table is mostly empty.

Key: `{name: "Sunset", uuid: "<uuid-v4>"}`.

Value (JSON):

```json
{
  "name": "Sunset",
  "thumbnailUuid": "<uuid>",         // points into db_custom_light_image
  "lightSetting": { /* _LightSettingToBuffer-compatible */ },
  "createdAt": 1763472000000
}
```

### 5.7 `db_img_share` / `db_light_share`

Cached cloud-share blobs. **Cloud download paths**:

```
https://api.rongyuan.tech:3814/v1/share/image
https://api.rongyuan.tech:3814/v1/share/light
```

(Fallback: `api2.qmk.top:3814`, `api3.qmk.top:3816`.)

Each downloaded blob is cached by content-UUID. The `CONFIG.json`
manifest's `shieldCommunitys: ["img"]` field (`CONFIG.json` per
company directory) lets a vendor disable the image-share community for
their re-branded build of the driver.

> Our reimplementation should not contact these endpoints. If we ever
> implement profile sharing, it should be:
>
> - Local-only export/import of `.ajprofile` JSON files, OR
> - A self-hostable backend the user opts into explicitly.

### 5.8 `audio`

Per-key audio clips for media-key macros (rare, mostly keyboard).
Mice with an audio key (some AJ-series have a "Discord push-to-talk"
key) use it.

Value: raw MP3/WAV bytes via `insertBufferToDB`.

### 5.9 `gun` — **anti-feature, do NOT replicate**

Recoil-control patterns ("no-recoil macros") tied to specific games.
Anti-cheat liability in any competitive title. The vendor app's
"Recoil Control" UI writes to this table + sends opcode `0x60` to the
device. **Do not implement.**

______________________________________________________________________

## 6 — Migration model

`sled 0.34.7` has its own database-format versioning embedded in the
sled binary header (visible in the strings:
`iot_driver_strings.txt:3984`):

```
This database was created using pagecache version <X>, but our pagecache
version is <Y>. Please perform an upgrade using the sled::Db::export and
sled::Db::import methods.
```

**The vendor app does NOT use sled's `export`/`import`.** If a future
version of `iot_driver.exe` bumps sled past 0.34.7, existing user
databases will fail to open and the daemon will likely crash on
startup. There is no schema-versioning at the JSON-value level either
— new fields are added by writing them; old reads default to
`undefined` and the renderer falls back to defaults.

This is fragile. Our Qt design should:

1. Embed a `schema_version` integer in every JSON value we persist.
1. Use `QSettings(IniFormat)` (no embedded database; no format
   migration risk).
1. For binary blobs (LCD wallpapers, macros), use plain files in
   `$XDG_DATA_HOME/ajazz/<vid>:<pid>/...` with `.json` siblings for
   metadata.

______________________________________________________________________

## 7 — Disk space estimate (per device, per user)

| Table                                   | Typical size |
| --------------------------------------- | ------------ |
| `device_type` (one row per active SKU)  | ~2 KB        |
| `CONFIG.option0` (per profile)          | ~500 B       |
| `CONFIG.option1` (per profile)          | ~300 B       |
| `macro` (per active slot)               | ~5–20 KB     |
| `screen_image` (per LCD frame)          | ~30–500 KB   |
| `db_img_share` (cached per favourite)   | ~50–200 KB   |
| `db_light_share` (cached per favourite) | ~1 KB        |
| `user`                                  | ~2 KB        |

A typical user with 1 AJ159 + 8 profiles + 5 macros + 1 LCD wallpaper
will occupy **~500 KB to 2 MB** of sled data. The sled segment-file
overhead pushes this to ~10–20 MB on disk due to the way sled
allocates fixed-size segment files (typically 256 MB by default;
sled's `Config::segment_size` setting controls this).

______________________________________________________________________

## 8 — Code corrections required

This document is informational only. The vendor's sled DB is
**not** something we should mirror in our codebase.

### 8.1 In our repo

| File                                                                                    | Change                                                                                                                                            |
| --------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/profiles/profile.hpp`                                                              | Add a `static constexpr std::uint8_t kSchemaVersion = 1;` and persist it in every JSON document we write — vendor's schema-free model is brittle. |
| `src/profiles/profile.cpp` (`Profile::saveToFile()`)                                    | Bake `"schema_version": kSchemaVersion` into the top-level JSON object.                                                                           |
| `docs/protocols/mouse/aj_series_vendor.md` §"Persistence model"                         | Reference this document for the full table-by-table breakdown.                                                                                    |
| `docs/research/builtin-plugin-categories.md` (or wherever profile-sharing is discussed) | Note that the vendor's cloud-share endpoints are **anti-features** and we do not plan to implement equivalents.                                   |

### 8.2 Catch2 tests

Persistence is currently file-based (`.ajprofile` JSON). The relevant
existing tests live in `tests/profiles/test_profile_io.cpp`. Add:

```cpp
TEST_CASE("Profile JSON: schema_version is written and validated on round-trip",
          "[profile][persistence]") {
    Profile p;
    p.deviceCodename = "aj159";
    p.saveToFile(tempFile);

    auto json = nlohmann::json::parse(readFile(tempFile));
    REQUIRE(json.contains("schema_version"));
    CHECK(json["schema_version"].get<int>() == Profile::kSchemaVersion);
}

TEST_CASE("Profile JSON: unknown future schema_version is rejected with a clear error",
          "[profile][persistence]") {
    auto json = makeMinimalProfileJson();
    json["schema_version"] = 999;
    writeFile(tempFile, json.dump());
    REQUIRE_THROWS_AS(Profile::loadFromFile(tempFile), Profile::SchemaMismatch);
}
```

______________________________________________________________________

## 9 — References

| Subject                                  | File:Line                                                    |
| ---------------------------------------- | ------------------------------------------------------------ |
| sled crate version                       | `iot_driver_strings.txt:1344, 1684` (`sled-0.34.7`)          |
| sled storage path strings                | `iot_driver_strings.txt:1360` (`./iot_dbiot_db`)             |
| iot_db default fallback                  | `iot_driver_strings.txt:3984` (sled pagecache version error) |
| Renderer DBPATH enum                     | `dist/static/js/main_beautified.js:57534`                    |
| `insertBufferToDB` / `insertDataToDB`    | `dist/static/js/main_beautified.js:57537, 57548`             |
| `getItemFromDb` (with base64 round-trip) | `dist/static/js/main_beautified.js:57569`                    |
| `node-machine-id` import                 | `package.json:23`                                            |
| Cloud endpoint references                | `dist/static/js/main_beautified.js:53299–53302`              |
