# Adding a Device

This guide walks through the process of adding support for a new AJAZZ
or Mirabox device. It assumes you've read
[`../protocols/REVERSE_ENGINEERING.md`](../protocols/REVERSE_ENGINEERING.md)
and that you have the device physically available for hot-plug + USB
capture.

## TL;DR — minimal touch list

For a new Stream Dock variant "AKP777" with 8 LCD keys + 2 encoders:

| Step | File                                                   | Why                                                           |
| ---- | ------------------------------------------------------ | ------------------------------------------------------------- |
| 1    | `docs/_data/devices.yaml`                              | Single source of truth; flows into README, wiki, maturity map |
| 2    | `docs/protocols/streamdeck/akp777.md`                  | Wire-format spec from your USB captures                       |
| 3    | `src/devices/streamdeck/src/akp777_protocol.{hpp,cpp}` | Pure-byte builders + parser (no I/O)                          |
| 4    | `src/devices/streamdeck/src/akp777.cpp`                | Backend `IDevice` + capability mix-ins                        |
| 5    | `src/devices/streamdeck/include/...streamdeck.hpp`     | Export the factory                                            |
| 6    | `src/devices/streamdeck/src/register.cpp`              | Register the factory + VID:PID with the registry              |
| 7    | `tests/unit/test_akp777_protocol.cpp`                  | Byte-level regression coverage                                |
| 8    | `tests/unit/CMakeLists.txt`                            | Add the new test target                                       |

That's it. **The QML sidebar, maturity glyph, time-sync UI, battery
poll, and plugin catalog all auto-detect the new device** via interface
discovery — you do NOT touch `device_model.cpp`, `time_sync_service.cpp`,
`application.cpp`, or any QML file.

## 1. Identify the device

```bash
lsusb -v                        # Linux
system_profiler SPUSBDataType   # macOS
# Windows: Device Manager → Hardware Ids
```

Note the USB **Vendor ID** and **Product ID**. Cross-reference with
existing entries in `src/devices/*/src/register.cpp` to avoid
duplicates.

## 2. Pick the right family

| If the device has…                | Put it under             |
| --------------------------------- | ------------------------ |
| LCD keys and/or dials             | `src/devices/streamdeck` |
| Alphanumeric keys and RGB matrix  | `src/devices/keyboard`   |
| DPI stages, scroll wheel, buttons | `src/devices/mouse`      |

## 3. Capture traffic

Follow the procedure in `REVERSE_ENGINEERING.md`. Save the annotated
decode as `docs/protocols/<family>/<codename>.md`.

## 4. Declare the device in `devices.yaml`

This is the **single source of truth** for the README support matrix,
the wiki, and the in-app maturity glyph. Add a block under `devices:`:

```yaml
- codename: akp777
  family: streamdeck                    # "streamdeck" | "keyboard" | "mouse" | "dongle"
  name: "AJAZZ AKP777"
  vid: "0x0300"
  pid: "0x7770"
  keys: 8
  encoders: 2
  maturity: scaffolded                  # "scaffolded" | "probed" | "partial" | "functional" | "verified"
  capabilities: [display, rgb, encoder, clock]
  protocol_doc: docs/protocols/streamdeck/akp777.md
  notes: "8 LCD keys (2x4) + 2 encoders; 80x80 JPEG keys (Rot0)."
```

Run `python3 scripts/generate-docs.py` (or just commit — the pre-commit
hook does it). This regenerates **three artefacts**:

- `README.md` device tables + stats
- `docs/wiki/Supported-Devices.md`
- `src/app/src/device_maturity_map.generated.hpp` — the C++ map the
  QML sidebar reads. (You do NOT hand-edit this file.)

CI fails loudly if the generated header is out of sync, so this is
hard to forget.

## 5. Write the protocol module

Create `src/devices/<family>/src/<codename>_protocol.hpp` and
`_protocol.cpp`. Keep these files free of I/O — they only build/parse
byte buffers. Example structure:

```cpp
namespace ajazz::streamdeck::akp777 {
    inline constexpr std::uint16_t VendorId  = 0x0300;
    inline constexpr std::uint16_t ProductId = 0x7770;
    std::array<std::uint8_t, 512> buildSetBrightness(std::uint8_t pct);
    std::optional<KeyEvent>       parseInputReport(std::span<std::uint8_t const>);
}
```

## 6. Write unit tests for the protocol

Mirror every public helper in `tests/unit/test_<codename>_protocol.cpp`:

- Check the exact byte offsets against the spec.
- Test edge cases: clamping, malformed input, unknown command.
- ASCII-only `TEST_CASE` / `SECTION` titles (pre-commit hook enforces
  this — Windows CI mangles em-dash / arrow / ≠ / § via cmd.exe code
  page).

## 7. Implement the backend

Create `src/devices/<family>/src/<codename>.cpp` and a factory
`makeAkp777` exposed from `<family>.hpp`. Implement
`ajazz::core::IDevice` plus the capability mix-ins your device
supports:

```cpp
class Akp777 final : public core::IDevice,
                     public core::IDisplayCapable,    // <- LCD keys
                     public core::IRgbCapable,        // <- RGB backlight
                     public core::IEncoderCapable,    // <- dials
                     public core::IClockCapable {     // <- firmware RTC
    // ...
};
```

The capability interfaces are declared in
[`src/core/include/ajazz/core/capabilities.hpp`](../../src/core/include/ajazz/core/capabilities.hpp).
Pick only what the device actually supports — `dynamic_cast` at the
call site checks for nullptr, so missing capabilities degrade
gracefully (the QML sidebar dims the affected control, no crash).

**Test seam**: always expose a `makeAkp777WithTransport(d, id, transport)`
sibling factory that accepts a `core::TransportPtr`. The MockTransport
test fixture (see
[`tests/unit/fixtures/mock_transport.hpp`](../../tests/unit/fixtures/mock_transport.hpp))
uses this to capture every `writeFeature()` payload without real HID.

## 8. Register the backend

Extend `src/devices/<family>/src/register.cpp`:

```cpp
reg.registerDevice(
    core::DeviceDescriptor{
        .vendorId  = akp777::VendorId,
        .productId = akp777::ProductId,
        .family    = core::DeviceFamily::StreamDeck,
        .model     = "AJAZZ AKP777",
        .codename  = "akp777",
        .keyCount  = 8,
        .gridColumns = 4,
        .encoderCount = 2,
        .hasRgb = true,
        .hasClock = true,
    },
    &makeAkp777);
```

The descriptor fields here feed the QML grid renderer (`keyCount`,
`gridColumns`, `encoderCount`) and the capability banners
(`hasClock`, `hasRgb`, …). They must stay in sync with the YAML
`capabilities:` list — there is no automated check for this yet
(tracked in `TODO.md` "device descriptor / YAML cross-check").

## 8.5 Device presentation (sidebar icon + product photo + name)

Two visual surfaces identify the device in the UI. Keep them consistent for
every new device:

### Sidebar family icon — automatic, nothing to do

Each sidebar row (`DeviceRow.qml`) shows a small leading icon chosen **by
`DeviceFamily`** from the bundled clean-room set
`resources/devices/device-{keyboard,mouse,streamdock}.svg`
(2→keyboard, 3→mouse, 1/0/else→generic Stream Dock). It is auto-selected — you
do **not** add a per-model icon. Just make sure the descriptor's
`.family` (and the YAML `family:`) is correct and the family icon is right.

### Device-pane product photo — one drop-in step

The device editor header (`ProfileEditor.qml`) shows, for the selected device:

```
<Product Name>            [ product photo ]
Editing: <codename>
```

The photo is rendered by `components/DeviceImage.qml` from a **bundled** image
(qrc, no runtime network). To give a new device a real photo:

1. Download the vendor product shot, **downsize it** (~256 px on the long
   edge — keep it well under ~80 KB; e.g. Shopify CDN supports `&width=256`)
   and save it as `resources/devices/products/product-<codename>.png` (PNG or
   JPG; CMake auto-globs the folder into the qrc via `CONFIGURE_DEPENDS`, no
   CMake edit needed). **A plain studio photo on a white background is fine** —
   `DeviceImage.qml` renders every photo on a small white rounded "thumbnail
   card", so the white backdrop reads as intentional on any theme. Do **not**
   try to alpha-key the background to transparency: white/light products (white
   keyboard, white mouse) cannot be colour-keyed without punching holes in the
   product, and a transferred mask from a darker colourway won't pixel-align
   (separate photo shoots). If you ever do have a genuinely pre-cut transparent
   PNG, it still works (it just sits on the white card).
1. Add one line to the `_productByCodename` map in `DeviceImage.qml`:
   `"<codename>": _img("product-<codename>.<ext>"),`
1. If the new codename is just a **variant** of a product already pictured
   (a 2.4G / dongle / rebadge SKU), add a prefix/alias rule in
   `_resolveProduct()` instead of a second copy of the same photo (see the
   `aj159_*` / `akp03*` / `ajazz_24g_8k` rules already there).

If you skip this, the header gracefully falls back to the per-family SVG — a
photo is **recommended but optional**. Never reference a remote URL: photos are
bundled so the UI works offline.

> **Asset hygiene:** these are downsized vendor product shots used only for
> in-app device identification. Keep them small and do not commit full-res
> marketing assets.

### Naming conventions (consistency rules)

- **`name` / descriptor `.model`** = the real human **product name** as a user
  would recognise it (e.g. `"AJAZZ AJ159 APEX (2.4G 8K)"`), shown large at the
  top of the editor. Do **not** ship a mode descriptor or placeholder as the
  name (the `0x5007` "2.4G 8K" → "AJ159 APEX" fix, 2026-05-22, was exactly this:
  prefer the product family name). If a live unit contradicts the RE catalogue,
  the hardware wins — fix the name and update the protocol/matrix doc.
- **`codename`** = a stable lowercase machine id (`akp05e`, `aj159_apex_wired`).
  It keys profiles, lookups, and the product-photo map, so **do not rename a
  codename once it has shipped** — add an alias rule instead.
- Keep the YAML `name:` / `codename:` and the `register.cpp` `.model` /
  `.codename` identical.

## 9. Integration tests

Drop a sanitized fragment of your capture into
`tests/integration/fixtures/<codename>/` and add a replay test in
`tests/integration/`.

## 10. Plugin catalog opt-in (optional)

If you want existing plugins to advertise compatibility with the new
device, add the codename to the relevant entries in
[`src/app/src/plugin_catalog_model.cpp`](../../src/app/src/plugin_catalog_model.cpp).
This is currently the only true hidden touch point in the
add-a-device flow — tracked in `TODO.md` "Generate plugin catalog
device lists from YAML" (the goal is to lift this into the YAML in a
later milestone so plugins can declare "supports any
encoder-capable device" instead of pinning specific codenames).

## 11. Auto-wired features

Once steps 4-8 are done, **the following work without further code
changes**:

- **QML sidebar row** — `DeviceModel::refresh()` re-enumerates the
  registry; the new row appears automatically.
- **Maturity glyph** — `device_maturity_map.generated.hpp` is
  regenerated from your YAML entry; the glyph turns the right colour
  (scaffolded/probed/partial/functional/verified).
- **Time-sync UI** — `TimeSyncService` does `dynamic_cast<IClockCapable*>`
  on any connected device; no per-device list to update. The new 15-min
  auto-sync timer (2026-05-18) picks up the device on its next tick.
- **Battery polling** — same pattern via `IBatteryCapable`.
- **Hot-plug detection** — `HotplugMonitor` matches on VID:PID alone;
  the new VID:PID lands the moment `register.cpp` is rebuilt.

## 12. Final checklist

- [ ] `docs/_data/devices.yaml` has a new entry with valid `maturity:`.
- [ ] `python3 scripts/generate-docs.py` ran (or pre-commit installed).
- [ ] `docs/protocols/<family>/<codename>.md` exists.
- [ ] `src/devices/<family>/src/<codename>_protocol.{hpp,cpp}` exists.
- [ ] `src/devices/<family>/src/<codename>.cpp` implements `IDevice` +
  relevant capability mix-ins, plus a `make<Name>WithTransport`
  test-seam factory.
- [ ] `src/devices/<family>/src/register.cpp` registers the descriptor, with
  `.model` set to the real **product name** (not a mode/placeholder) and the
  `.family` correct (drives the sidebar icon).
- [ ] (Recommended) a downsized product photo at
  `resources/devices/products/product-<codename>.{png,jpg}` + a line in
  `DeviceImage.qml`'s `_productByCodename` map (or a `_resolveProduct()` alias
  rule for a variant). Falls back to the family SVG if omitted.
- [ ] `tests/unit/test_<codename>_protocol.cpp` covers every builder
  with byte-level assertions (ASCII-only TEST_CASE titles).
- [ ] `tests/unit/CMakeLists.txt` adds the new test target.
- [ ] `CHANGELOG.md` "Unreleased" has a one-line entry.
- [ ] If Linux-specific udev rule needed, extend
  `resources/linux/70-ajazz.rules`.

## 13. Open the PR

Follow the checklist in `.github/pull_request_template.md` and request
a review. The CI matrix (Linux/macOS/Windows + Lint + CodeQL + Nightly

- Secret scan) will run automatically.
